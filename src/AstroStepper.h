
#ifndef ASTROSTEPPER_H
#define ASTROSTEPPER_H

#include <Arduino.h>

#define ASTROSTEPPER_VERSION "2.3.3"

/*
 * =============================================================
 * ASTROSTEPPER LIBRARY
 * =============================================================
 *
 * High precision stepper motor control library for Arduino (AVR)
 *
 * DESIGN GOALS
 * ------------
 * - Deterministic real-time step generation
 * - No cumulative frequency error (DDS)
 * - Strict acceleration control
 * - Mechanical backlash compensation
 * - Hardware efficiency (direct port access)
 *
 * -------------------------------------------------------------
 * PIN CONFIGURATION
 * -------------------------------------------------------------
 * setPins(STEP, DIR, ENABLE)
 *
 * Example:
 *   setPins(11, 8, 7);
 *      STEP   = pin 11  (STEP pulse output)
 *      DIR    = pin 8   (direction signal)
 *      ENABLE = pin 7   (driver enable)
 *
 * ENABLE LOGIC
 * ------------
 * ENABLE is ACTIVE LOW:
 *   LOW  → driver enabled
 *   HIGH → driver disabled
 *
 * -------------------------------------------------------------
 * SPEED CONVENTION (IMPORTANT)
 * -------------------------------------------------------------
 * Speed is SIGNED:
 *   +speed → forward rotation
 *   -speed → reverse rotation
 *
 * The sign directly controls the DIR pin.
 *
 * -------------------------------------------------------------
 * UNITS
 * -------------------------------------------------------------
 * Speed        : steps / second (SIGNED)
 * Acceleration : steps / second²
 * Backlash     : steps
 *
 * -------------------------------------------------------------
 * USAGE ORDER (CRITICAL)
 * -------------------------------------------------------------
 * 1. setPins()
 * 2. setAcceleration()
 * 3. setBacklash()
 * 4. setSpeed()
 *
 */

class AstroStepper
{
public:
    static void setPins(uint8_t stepPin, uint8_t dirPin, uint8_t enablePin);

    static void setSpeed(float speed_steps_s);
    static void setAcceleration(float accel_steps_s2);
    static void setBacklash(int32_t steps);

    static float getTargetSpeed();
    static float getCurrentSpeed();

    static void enable();
    static void disable();

    static void isrHandler();   // Do not call manually
};

#endif
