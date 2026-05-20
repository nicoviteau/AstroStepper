
/*
 * =============================================================
 * ASTROSTEPPER IMPLEMENTATION
 * =============================================================
 *
 * ARCHITECTURE OVERVIEW
 * ---------------------
 * This library is based on 3 independent blocks:
 *
 * 1. DDS (Direct Digital Synthesis)
 *    → produces an extremely precise average step frequency
 *
 * 2. Acceleration controller
 *    → ensures speed changes obey max acceleration
 *
 * 3. Backlash compensation
 *    → executes a real movement when direction changes
 *
 * NOTE:
 * Speed is SIGNED → sign = direction
 *
 */

#include "AstroStepper.h"
#include <avr/interrupt.h>

#define BACKLASH_SPEED_MAX 700.0f
#define BACKLASH_ACCEL_MAX 800


// ===================== PIN CONFIG =====================
static uint8_t stepPin = 11;
static uint8_t dirPin  = 8;
static uint8_t enablePin = 7;

static volatile uint8_t* stepPort;
static volatile uint8_t* dirPort;
static volatile uint8_t* enPort;

static uint8_t stepMask;
static uint8_t dirMask;
static uint8_t enMask;

#define STEP_HIGH() (*stepPort |= stepMask)
#define STEP_LOW()  (*stepPort &= ~stepMask)
#define DIR_HIGH()  (*dirPort |= dirMask)
#define DIR_LOW()   (*dirPort &= ~dirMask)
#define ENABLE_ON() (*enPort &= ~enMask)   // ACTIVE LOW
#define ENABLE_OFF()(*enPort |= enMask)

// ===================== TIMING =====================
#define F_ISR 16000UL
#define SCALE_FP ((uint32_t)((65536.0f / F_ISR) * 65536.0f))

// ===================== STATE MACHINE =====================
enum MotionState { NORMAL, BACKLASH };
static volatile MotionState motion_state = NORMAL;

// ===================== SPEED =====================
static volatile int32_t target_speed_fp = 0;
static volatile int32_t user_target_speed_fp = 0;
static volatile int32_t new_user_target_speed_fp = 0;
static volatile int32_t current_speed_fp = 0;
static volatile int32_t accel_fp = (int32_t)(100.0f * (1.0f / F_ISR) * 65536.0f); // Q16.16 Acceleration for 1 ISR cycle
static volatile int16_t accel_x2_Q8_8 = (int16_t)(BACKLASH_ACCEL_MAX << 9);	// Q8.8 double of Acceleration for 1 step ( = 2*A in 2*A*s approximation)

// ===================== BACKLASH =====================
static volatile int32_t backlash_steps = 0;
static volatile int32_t backlash_remaining = 0;
static volatile int32_t backlash_speed_max_fp = (int32_t)(BACKLASH_SPEED_MAX * 65536.0f);
static volatile int8_t previous_dir = 1;
static volatile int8_t changed_dir = 0;

// ===================== DDS =====================
static volatile uint32_t phase = 0;
static volatile uint32_t increment = 0;

// ===================== STEP GEN =====================
static volatile uint8_t step_pending = 0;
static volatile uint8_t step_state = 0;

// =============================================================
// CONFIGURATION
// =============================================================

void AstroStepper::setPins(uint8_t s,uint8_t d,uint8_t e)
{
    stepPin = s;
    dirPin  = d;
    enablePin = e;

    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    pinMode(enablePin, OUTPUT);

    stepPort = portOutputRegister(digitalPinToPort(stepPin));
    dirPort  = portOutputRegister(digitalPinToPort(dirPin));
    enPort   = portOutputRegister(digitalPinToPort(enablePin));

    stepMask = digitalPinToBitMask(stepPin);
    dirMask  = digitalPinToBitMask(dirPin);
    enMask   = digitalPinToBitMask(enablePin);
}

// =============================================================
// USER PARAMETERS
// =============================================================

void AstroStepper::setBacklash(int32_t steps, float vmax, float accel)
{
    backlash_steps = steps;
	backlash_speed_max_fp = (int32_t)(vmax * 65536.0f); // Q16.16
	accel_x2_Q8_8 = (int16_t)(accel * 256.0f * 2.0f);	// Q8.8 double of Acceleration for 1 step (for 2*A*s calculus) pour backlash => acceleration max
}

void AstroStepper::setAcceleration(float accel)
{
    if (accel < 0) accel = -accel;
    accel_fp = (int32_t)(accel * (1.0f / F_ISR) * 65536.0f); // Q16.16 Acceleration for 1 ISR cycle
	
}

void AstroStepper::setSpeed(float speed)
{
    new_user_target_speed_fp = (int32_t)(speed * 65536.0f);
	
	if(motion_state == NORMAL)
		user_target_speed_fp = new_user_target_speed_fp;
	else
		if((user_target_speed_fp * new_user_target_speed_fp)>=0)
			user_target_speed_fp = new_user_target_speed_fp;
		
		
	
    enable();
}

float AstroStepper::getTargetSpeed()
{
    return (float)user_target_speed_fp / 65536.0f;
}

float AstroStepper::getCurrentSpeed()
{
    return (float)current_speed_fp / 65536.0f;
}

// =============================================================
// TIMER
// =============================================================

void AstroStepper::enable()
{
    cli();

    ENABLE_ON();

    TCCR2A = (1 << WGM21);
    TCCR2B = (1 << CS22);
    OCR2A  = 15;

    TIMSK2 |= (1 << OCIE2A);

    sei();
}

void AstroStepper::disable()
{
    ENABLE_OFF();
}

ISR(TIMER2_COMPA_vect)
{
    AstroStepper::isrHandler();
}

// =============================================================
// ISR CORE
// =============================================================

void AstroStepper::isrHandler()
{	

    // ---- previous dir memorization ----
    previous_dir = (current_speed_fp >= 0) ? 1 : -1;
	
    // ---- Mode ----
	if(motion_state == NORMAL)
	{	// === NORMAL MODE ===
        target_speed_fp = user_target_speed_fp;

		// ---- Accel ramp ----
		int32_t diff = target_speed_fp - current_speed_fp;
		if(diff > accel_fp) current_speed_fp += accel_fp;
		else if(diff < -accel_fp) current_speed_fp -= accel_fp;
		else current_speed_fp = target_speed_fp;

		// ---- DIR set and inversion detection ----
		if(current_speed_fp >= 0) 
		{
			DIR_HIGH();
			changed_dir = (previous_dir < 0) ? 1 : 0;
		}
		else 
		{
			DIR_LOW();
			changed_dir = (previous_dir > 0) ? 1 : 0;
		}
	
		//---- backlash mode activation ----
		if((changed_dir == 1) && (motion_state == NORMAL))
		{
			backlash_remaining = backlash_steps;
			motion_state = BACKLASH;
		}
	}
	else
	{
		// === BACKLASH MODE ===
		int16_t v8;		// Q8.8 current_speed
		int16_t vend8;	// Q8.8 user_target_speed_fp
		
		uint16_t v_sq;		// V²
		uint16_t vend_sq;	// Vend²
		uint32_t v_lim_sq;	// Vlimit² to not exceed
	
		// --- réduction Q16.16 → Q8.8 ---
		v8    = (int16_t)(current_speed_fp >> 8);
		vend8 = (int16_t)(user_target_speed_fp >> 8);
		
		// --- v² ---
		asm volatile (
			"mul %1, %1\n\t"
			"movw %0, r0\n\t"
			"clr r1\n\t"
			: "=&r" (v_sq)
			: "r" (v8)
			: "r0"
		);

		// --- vend² ---
		asm volatile (
			"mul %1, %1\n\t"
			"movw %0, r0\n\t"
			"clr r1\n\t"
			: "=&r" (vend_sq)
			: "r" (vend8)
			: "r0"
		);
		
		// --- 2*A*s approx --- (s=backlash_remaining)
		int32_t decel = (int32_t)accel_x2_Q8_8 * backlash_remaining;   // 2*A*s    Q8.8 * int → Q8.8 (s=backlash_remaining)

		// --- limite ---
		v_lim_sq = (uint32_t)vend_sq + (uint32_t)decel;

		// --- décision et calcul nouvelle vitesse ---
		if (v_sq < v_lim_sq)
		{	
			// Acceleration up to Vmax
			if (current_speed_fp >=0) 
				current_speed_fp = min(current_speed_fp + accel_fp , backlash_speed_max_fp);
			else
				current_speed_fp = max(current_speed_fp - accel_fp , -backlash_speed_max_fp);			
		}
		else
		{
			// Brake to Vend
			if (current_speed_fp >=0) 
				current_speed_fp = max(current_speed_fp - accel_fp , user_target_speed_fp);
			else
				current_speed_fp = min(current_speed_fp + accel_fp , -user_target_speed_fp);
		}
				
	}

    // ---- DDS ----
    int32_t s = current_speed_fp;
    uint32_t abs_speed = (s ^ (s >> 31)) - (s >> 31);
    //increment = ((uint64_t)abs_speed * SCALE_FP) >> 16;
	
	uint16_t xH = abs_speed >> 16;                                  // optimisation multiplication 64 bits 
	uint16_t xL = abs_speed & 0xFFFF;                               // optimisation multiplication 64 bits 
																	// optimisation multiplication 64 bits 
	// Produit 16x32 → 32 bits (optimisé par gcc)                   // optimisation multiplication 64 bits 
	uint32_t term1 = (uint32_t)xH * SCALE_FP;                       // optimisation multiplication 64 bits 
																	// optimisation multiplication 64 bits 
	// Produit 16x32 → 32 bits puis >>16                            // optimisation multiplication 64 bits 
	uint32_t term2 = ((uint32_t)xL * SCALE_FP) >> 16;               // optimisation multiplication 64 bits 
																	// optimisation multiplication 64 bits 
	increment = term1 + term2;                                      // optimisation multiplication 64 bits 

    uint32_t old = phase;
    phase += increment;

    if (phase < old && step_pending < 255)
        step_pending++;

    // ---- STEP ----
    if(step_state == 0 && step_pending)
    {
        STEP_HIGH();
        step_state = 1;
    }
    else if(step_state == 1)
    {
        STEP_LOW();
        step_pending--;
        step_state = 0;

        if(motion_state == BACKLASH && backlash_remaining > 0)
        {
            backlash_remaining--;
            if(backlash_remaining == 0)
                motion_state = NORMAL;
        }
    }
}
