
/*
 * =============================================================
 * ASTROSTEPPER IMPLEMENTATION
 * =============================================================
 *
 * ARCHITECTURE OVERVIEW
 * ---------------------
 * This library implements a stepper motor controller with 3 independent blocks:
 *
 * 1. DDS (Direct Digital Synthesis)
 *    → Produces an extremely precise average step frequency using phase accumulation
 *    → Eliminates jitter from discretized step timing
 *
 * 2. Acceleration Controller
 *    → Ensures smooth speed ramps that obey maximum acceleration constraints
 *    → Prevents mechanical stress and missed steps
 *
 * 3. Backlash Compensation
 *    → Automatically compensates for mechanical backlash when direction changes
 *    → Executes a fixed-distance movement before responding to user commands
 *
 * SPEED REPRESENTATION
 * --------------------
 * Speed is SIGNED with Q16.16 fixed-point format:
 *   - Sign bit indicates direction (+ = forward, - = backward)
 *   - Magnitude represents steps per ISR cycle
 *
 */

#include "AstroStepper.h"
#include <avr/interrupt.h>

/* Backlash compensation limits */
#define BACKLASH_SPEED_MAX 700.0f   /* Maximum speed during backlash compensation */
#define BACKLASH_ACCEL_MAX 800      /* Maximum acceleration during backlash compensation */

// ===================== PIN CONFIGURATION =====================
/* Hardware pins (default: Arduino Uno configuration) */
static uint8_t stepPin = 11;        /* Step pulse output */
static uint8_t dirPin  = 8;         /* Direction control output */
static uint8_t enablePin = 7;       /* Enable/disable output (active low) */

/* Port addresses and bit masks for fast direct I/O access */
static volatile uint8_t* stepPort;  /* Direct port register for step pin */
static volatile uint8_t* dirPort;   /* Direct port register for direction pin */
static volatile uint8_t* enPort;    /* Direct port register for enable pin */

static uint8_t stepMask;            /* Bit mask for step pin on its port */
static uint8_t dirMask;             /* Bit mask for direction pin on its port */
static uint8_t enMask;              /* Bit mask for enable pin on its port */

/* Fast I/O macros - bit manipulation instead of digitalWrite() */
#define STEP_HIGH() (*stepPort |= stepMask)     /* Assert step pulse */
#define STEP_LOW()  (*stepPort &= ~stepMask)    /* Release step pulse */
#define DIR_HIGH()  (*dirPort |= dirMask)       /* Forward direction */
#define DIR_LOW()   (*dirPort &= ~dirMask)      /* Reverse direction */
#define ENABLE_ON() (*enPort &= ~enMask)        /* Enable motor (active low) */
#define ENABLE_OFF()(*enPort |= enMask)         /* Disable motor */

// ===================== TIMER & ISR CONFIGURATION =====================
/* ISR frequency: 16 kHz (Timer2 interrupt rate) */
#define F_ISR 16000UL

/* DDS scale factor for converting speed to phase increment
   SCALE_FP = (65536 / F_ISR) * 65536 = (2^16 / F_ISR) * 2^16
   This maps speed in steps/sec to phase accumulator increment
*/
#define SCALE_FP ((uint32_t)((65536.0f / F_ISR) * 65536.0f))

// ===================== STATE MACHINE =====================
enum MotionState { 
    NORMAL,     /* Normal operation: apply user target speed with acceleration control */
    BACKLASH    /* Backlash compensation: move fixed distance before normal operation */
};
static volatile MotionState motion_state = NORMAL;

// ===================== SPEED & ACCELERATION =====================
/* All speed variables use Q16.16 fixed-point format (sign + 48 bits) */

static volatile int32_t target_speed_fp = 0;           /* Desired speed (before backlash logic) */
static volatile int32_t user_target_speed_fp = 0;      /* User-requested speed from API */
static volatile int32_t new_user_target_speed_fp = 0;  /* Buffered new speed request */
static volatile int32_t current_speed_fp = 0;          /* Actual current speed (may be ramping) */

/* Acceleration for each ISR cycle (Q16.16 format)
   accel_fp = accel_steps_per_sec² / (F_ISR / 65536)
   Default: 100 steps/sec² = 6553.6 LSBs per ISR cycle
*/
static volatile int32_t accel_fp = (int32_t)(100.0f * (1.0f / F_ISR) * 65536.0f);

/* Acceleration for backlash compensation (Q8.8 format, doubled)
   Used in approximation: v² = vend² + 2*A*s
   Factor of 2 is pre-multiplied to avoid extra computation
*/
static volatile int16_t accel_x2_Q8_8 = (int16_t)(BACKLASH_ACCEL_MAX << 9);

// ===================== BACKLASH COMPENSATION STATE =====================
static volatile int32_t backlash_steps = 0;            /* Backlash distance in steps */
static volatile int32_t backlash_remaining = 0;        /* Steps left to compensate in current cycle */
static volatile int32_t backlash_speed_max_fp = (int32_t)(BACKLASH_SPEED_MAX * 65536.0f); /* Q16.16 */

static volatile int8_t previous_dir = 1;               /* Direction from previous ISR cycle (+1 or -1) */
static volatile int8_t changed_dir = 0;                /* Flag: direction change detected */

// ===================== DDS (DIRECT DIGITAL SYNTHESIS) =====================
/* Phase accumulator algorithm for jitter-free step generation
   - phase: 32-bit accumulator (wraps at 2^32)
   - increment: How much phase increases per ISR cycle
   - Overflow triggers step pulse
*/
static volatile uint32_t phase = 0;         /* Phase accumulator */
static volatile uint32_t increment = 0;     /* Phase increment per ISR cycle (derived from speed) */

// ===================== STEP PULSE GENERATION =====================
/* Two-state step pulse generator
   ISR alternates between: STEP_HIGH → STEP_LOW
   step_pending: queue of pending steps (up to 255)
   step_state: current state (0=low, 1=high)
*/
static volatile uint8_t step_pending = 0;   /* Number of pending step pulses */
static volatile uint8_t step_state = 0;     /* Current step pulse state (0=LOW, 1=HIGH) */

// =============================================================
// CONFIGURATION FUNCTIONS
// =============================================================

void AstroStepper::setPins(uint8_t s, uint8_t d, uint8_t e)
{
    /* Configure which Arduino pins to use for step/direction/enable control
       Reads port configuration from Arduino core and pre-computes bit masks
       for fast I/O in ISR
    */
    stepPin = s;
    dirPin  = d;
    enablePin = e;

    /* Configure pins as outputs */
    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    pinMode(enablePin, OUTPUT);

    /* Pre-compute port addresses and bit masks for fast bit manipulation in ISR */
    stepPort = portOutputRegister(digitalPinToPort(stepPin));
    dirPort  = portOutputRegister(digitalPinToPort(dirPin));
    enPort   = portOutputRegister(digitalPinToPort(enablePin));

    stepMask = digitalPinToBitMask(stepPin);
    dirMask  = digitalPinToBitMask(dirPin);
    enMask   = digitalPinToBitMask(enablePin);
}

// =============================================================
// USER PARAMETER CONFIGURATION
// =============================================================

void AstroStepper::setBacklash(int32_t steps, float vmax, float accel)
{
    /* Configure backlash compensation parameters
       
       steps: Distance (in motor steps) to compensate when direction reverses
       vmax:  Maximum speed allowed during backlash compensation
       accel: Acceleration used during backlash compensation
       
       NOTE: Backlash motion uses different acceleration than normal operation
       to prevent overshoot and mechanical shock
    */
    backlash_steps = steps;
    
    /* Convert max velocity to Q16.16 fixed-point */
    backlash_speed_max_fp = (int32_t)(vmax * 65536.0f);
    
    /* Convert acceleration to Q8.8, pre-multiplied by 2 for v²=vend²+2*A*s formula */
    accel_x2_Q8_8 = (int16_t)(accel * 256.0f * 2.0f);
}

void AstroStepper::setAcceleration(float accel)
{
    /* Set maximum acceleration for normal operation (always positive)
       
       accel: Maximum acceleration in steps/second²
       
       Internally converted to Q16.16 format normalized to ISR period:
       - Ensures consistent step ramps regardless of ISR frequency
       - Applied once per ISR cycle to current_speed_fp
    */
    if (accel < 0) accel = -accel;  /* Force positive (work with magnitude) */
    
    /* Convert to ISR-cycle units in Q16.16 format */
    accel_fp = (int32_t)(accel * (1.0f / F_ISR) * 65536.0f);
}

void AstroStepper::setSpeed(float speed)
{
    /* Request a new target speed for the motor
       
       speed: Target speed in steps/second (positive=forward, negative=reverse)
       
       Logic:
       - In NORMAL mode: immediately update target speed
       - In BACKLASH mode: only update if same direction (prevents zigzag)
       - Automatically enables motor on every call
    */
    new_user_target_speed_fp = (int32_t)(speed * 65536.0f);
    
    if (motion_state == NORMAL)
    {
        /* Direct mode: apply speed immediately */
        user_target_speed_fp = new_user_target_speed_fp;
    }
    else
    {
        /* Backlash mode: only accept same-direction commands to avoid reversing
           Check: (old_speed * new_speed) >= 0 → same sign or either is zero
        */
        if ((user_target_speed_fp * new_user_target_speed_fp) >= 0)
            user_target_speed_fp = new_user_target_speed_fp;
    }
    
    enable();
}

float AstroStepper::getTargetSpeed()
{
    /* Query the currently-requested target speed (before acceleration ramp) */
    return (float)user_target_speed_fp / 65536.0f;
}

float AstroStepper::getCurrentSpeed()
{
    /* Query the actual current speed (may be different if ramping) */
    return (float)current_speed_fp / 65536.0f;
}

// =============================================================
// TIMER SETUP & ISR INITIALIZATION
// =============================================================

void AstroStepper::enable()
{
    /* Start the timer and enable motor
       
       Configuration:
       - Timer2 in CTC mode (Compare Timer Capture)
       - Prescaler = 64 (gives ~16 kHz ISR frequency on 16 MHz Arduino)
       - Compare match triggers ISR
    */
    cli();  /* Disable interrupts during setup */

    ENABLE_ON();  /* Assert motor enable pin */

    TCCR2A = (1 << WGM21);  /* CTC mode */
    TCCR2B = (1 << CS22);   /* Prescaler = 64 */
    OCR2A  = 15;            /* Compare value (16 MHz / 64 / 16 = ~15.6 kHz) */

    TIMSK2 |= (1 << OCIE2A);  /* Enable compare interrupt */

    sei();  /* Re-enable interrupts */
}

void AstroStepper::disable()
{
    /* Disable motor (deassert enable pin) */
    ENABLE_OFF();
}

ISR(TIMER2_COMPA_vect)
{
    /* Timer2 compare interrupt service routine entry point */
    AstroStepper::isrHandler();
}

// =============================================================
// ISR CORE LOGIC (CALLED 16,000 TIMES PER SECOND)
// =============================================================

void AstroStepper::isrHandler()
{
    /* Main control loop executed at 16 kHz
       Sequence:
       1. Update direction memory
       2. Execute state machine (NORMAL or BACKLASH mode)
       3. Apply DDS phase accumulation
       4. Generate step pulses
    */

    // ========== 1. DIRECTION TRACKING ==========
    /* Remember direction from this cycle (used next cycle to detect reversals) */
    previous_dir = (current_speed_fp >= 0) ? 1 : -1;

    // ========== 2. STATE MACHINE ==========
    if (motion_state == NORMAL)
    {
        /* ===== NORMAL OPERATION MODE =====
           Apply user-requested speed with smooth acceleration ramps
        */
        target_speed_fp = user_target_speed_fp;

        /* --- Smooth Acceleration Ramp ---
           Limits speed change to prevent mechanical stress
           - If too far from target: ramp by accel_fp
           - Otherwise: reach target exactly
        */
        int32_t diff = target_speed_fp - current_speed_fp;
        if (diff > accel_fp)
            current_speed_fp += accel_fp;        /* Still accelerating */
        else if (diff < -accel_fp)
            current_speed_fp -= accel_fp;        /* Still decelerating */
        else
            current_speed_fp = target_speed_fp;  /* Target reached exactly */

        /* --- Set Motor Direction & Detect Reversals ---
           Extract direction from speed sign and detect when direction changes
        */
        if (current_speed_fp >= 0)
        {
            DIR_HIGH();  /* Forward direction */
            changed_dir = (previous_dir < 0) ? 1 : 0;  /* Was moving backward? */
        }
        else
        {
            DIR_LOW();   /* Reverse direction */
            changed_dir = (previous_dir > 0) ? 1 : 0;  /* Was moving forward? */
        }

        /* --- Enter Backlash Compensation When Direction Reverses ---
           When motor reverses, trigger backlash compensation sequence
        */
        if ((changed_dir == 1) && (motion_state == NORMAL))
        {
            backlash_remaining = backlash_steps;
            motion_state = BACKLASH;
        }
    }
    else
    {
        /* ===== BACKLASH COMPENSATION MODE =====
           Execute fixed-distance movement at controlled speed/acceleration
           Uses kinematic approximation: v² = vend² + 2*A*s
           to smoothly transition from max speed to target speed over backlash distance
        */

        /* Local speed variables in Q8.8 format (smaller precision, sufficient for sqrt) */
        int16_t v8;         /* Current speed, Q8.8 */
        int16_t vend8;      /* Target speed, Q8.8 */

        uint16_t v_sq;      /* (current_speed)² for sqrt approximation */
        uint16_t vend_sq;   /* (target_speed)² for sqrt approximation */
        uint32_t v_lim_sq;  /* (v_limit)² = upper speed bound due to remaining distance */

        /* --- Convert from Q16.16 to Q8.8 ---
           Right-shift by 8 bits: Q16.16 → Q8.8
        */
        v8    = (int16_t)(current_speed_fp >> 8);
        vend8 = (int16_t)(user_target_speed_fp >> 8);

        /* --- Compute v² using inline AVR assembly (faster than C multiplication)
           mul: 8×8 → 16 bits (result in r1:r0)
           movw: Move word r0→r1 to destination
           clr r1: Clear upper byte (keep only lower 16 bits)
        */
        asm volatile(
            "mul %1, %1\n\t"      /* Multiply v8 * v8 → r1:r0 */
            "movw %0, r0\n\t"     /* Move result to v_sq */
            "clr r1\n\t"          /* Clear carry register */
            : "=&r" (v_sq)
            : "r" (v8)
            : "r0"
        );

        /* --- Compute vend² using inline AVR assembly ---
           Same as v² computation above
        */
        asm volatile(
            "mul %1, %1\n\t"      /* Multiply vend8 * vend8 → r1:r0 */
            "movw %0, r0\n\t"     /* Move result to vend_sq */
            "clr r1\n\t"          /* Clear carry register */
            : "=&r" (vend_sq)
            : "r" (vend8)
            : "r0"
        );

        /* --- Compute velocity limit from remaining distance ---
           Kinematic formula: v² = vend² + 2*A*s
           If we decelerate with acceleration 'A' over distance 's',
           what velocity should we reach to end at 'vend'?
           
           accel_x2_Q8_8 = 2*A (pre-multiplied to save one instruction)
           decel = 2*A*s = accel_x2_Q8_8 * backlash_remaining
        */
        int32_t decel = (int32_t)accel_x2_Q8_8 * backlash_remaining;

        /* Speed limit squared: must decelerate to vend² by end of backlash */
        v_lim_sq = (uint32_t)vend_sq + (uint32_t)decel;

        /* --- Speed Control Decision ---
           If current speed is too slow for available distance:
             → Accelerate toward backlash_speed_max (or decelerate to vend if negative)
           If current speed is too fast:
             → Decelerate toward vend over remaining distance
        */
        if (v_sq < v_lim_sq)
        {
            /* Not enough speed for remaining distance: accelerate to max */
            if (current_speed_fp >= 0)
            {
                /* Forward: increase speed but cap at backlash_speed_max_fp */
                current_speed_fp = min(current_speed_fp + accel_fp, backlash_speed_max_fp);
            }
            else
            {
                /* Reverse: decrease speed but cap magnitude at backlash_speed_max_fp */
                current_speed_fp = max(current_speed_fp - accel_fp, -backlash_speed_max_fp);
            }
        }
        else
        {
            /* Too much speed for remaining distance: decelerate to target */
            if (current_speed_fp >= 0)
            {
                /* Forward: decrease speed but stay above user_target_speed_fp */
                current_speed_fp = max(current_speed_fp - accel_fp, user_target_speed_fp);
            }
            else
            {
                /* Reverse: increase speed but stay below -user_target_speed_fp */
                current_speed_fp = min(current_speed_fp + accel_fp, -user_target_speed_fp);
            }
        }
    }

    // ========== 3. DDS PHASE ACCUMULATION ==========
    /* Convert signed speed to unsigned magnitude for DDS calculation */
    int32_t s = current_speed_fp;
    uint32_t abs_speed = (s ^ (s >> 31)) - (s >> 31);  /* Compute |speed| efficiently */

    /* --- Optimized 32-bit multiplication: (64-bit result) * 32-bit ÷ 2^16 ---
       Standard formula:  increment = (abs_speed * SCALE_FP) >> 16
       
       Optimized to use 16×32→32 multiplies (faster on AVR than full 32×32):
       - Split abs_speed into xH (high 16 bits) and xL (low 16 bits)
       - Compute: (xH << 16) * SCALE_FP + xL * SCALE_FP
       - Combine results with appropriate shifts
    */
    uint16_t xH = abs_speed >> 16;                    /* High 16 bits of speed */
    uint16_t xL = abs_speed & 0xFFFF;                 /* Low 16 bits of speed */

    uint32_t term1 = (uint32_t)xH * SCALE_FP;         /* 16×32 multiply → high contribution */
    uint32_t term2 = ((uint32_t)xL * SCALE_FP) >> 16; /* 16×32 multiply with shift → low contribution */

    increment = term1 + term2;  /* Combine contributions */

    /* --- Phase Accumulation with Step Detection ---
       Accumulator wraps at 2^32 boundary (no explicit modulo needed)
       Overflow → step pulse (multiple overflows = multiple pending steps)
    */
    uint32_t old = phase;
    phase += increment;

    /* Did we overflow the 32-bit accumulator? (and not already full) */
    if (phase < old && step_pending < 255)
        step_pending++;  /* Queue pending step pulse */

    // ========== 4. STEP PULSE GENERATION ==========
    /* Two-state generator: STEP_HIGH → STEP_LOW
       Each ISR cycle either asserts or releases the step pulse
       step_state toggles: 0 (LOW) → 1 (HIGH) → 0 (LOW) → ...
    */
    if (step_state == 0 && step_pending)
    {
        /* Transition LOW→HIGH: assert step pulse and queue next event */
        STEP_HIGH();
        step_state = 1;
    }
    else if (step_state == 1)
    {
        /* Transition HIGH→LOW: release step pulse and decrement queue */
        STEP_LOW();
        step_pending--;
        step_state = 0;

        /* --- Backlash Distance Tracking ---
           Each step pulse (HIGH→LOW transition) counts as one step
           Decrement remaining backlash distance
        */
        if (motion_state == BACKLASH && backlash_remaining > 0)
        {
            backlash_remaining--;
            
            /* Backlash compensation complete: return to NORMAL mode */
            if (backlash_remaining == 0)
                motion_state = NORMAL;
        }
    }
}
