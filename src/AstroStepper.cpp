
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
static volatile int32_t current_speed_fp = 0;
static volatile int32_t accel_fp = 0;

// ===================== BACKLASH =====================
static volatile int32_t backlash_steps = 0;
static volatile int32_t backlash_remaining = 0;
static volatile int32_t backlash_target_speed_fp = 0;
static volatile int8_t current_dir = 1;

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

void AstroStepper::setBacklash(int32_t steps)
{
    backlash_steps = steps;
}

void AstroStepper::setAcceleration(float accel)
{
    if (accel < 0) accel = -accel;
    accel_fp = (int32_t)(accel * (1.0f / F_ISR) * 65536.0f);
}

void AstroStepper::setSpeed(float speed)
{
    user_target_speed_fp = (int32_t)(speed * 65536.0f);
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
    // ---- Direction detection ----
    int8_t new_dir = (user_target_speed_fp >= 0) ? 1 : -1;

    if(new_dir != current_dir && motion_state == NORMAL)
    {
        current_dir = new_dir;
        backlash_remaining = backlash_steps;
        motion_state = BACKLASH;
        backlash_target_speed_fp = accel_fp * 200;
    }

    // ---- Mode ----
    if(motion_state == BACKLASH)
        target_speed_fp = backlash_target_speed_fp * current_dir;
    else
        target_speed_fp = user_target_speed_fp;

    // ---- Accel ramp ----
    int32_t diff = target_speed_fp - current_speed_fp;
    if(diff > accel_fp) current_speed_fp += accel_fp;
    else if(diff < -accel_fp) current_speed_fp -= accel_fp;
    else current_speed_fp = target_speed_fp;

    // ---- DIR ----
    if(current_speed_fp >= 0) DIR_HIGH(); else DIR_LOW();

    // ---- DDS ----
    int32_t s = current_speed_fp;
    uint32_t abs_speed = (s ^ (s >> 31)) - (s >> 31);
    increment = ((uint64_t)abs_speed * SCALE_FP) >> 16;

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
