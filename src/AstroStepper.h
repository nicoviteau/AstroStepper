
#ifndef ASTROSTEPPER_H
#define ASTROSTEPPER_H

#include <Arduino.h>

#define ASTROSTEPPER_VERSION "2.4.0"

/**
 * ============================================================================
 *                          ASTROSTEPPER LIBRARY
 * ============================================================================
 *
 * High-precision stepper motor control library for Arduino (AVR microcontrollers)
 *
 * Designed for astronomical mounts, precision positioning, and real-time
 * applications requiring deterministic motor control without timing errors.
 *
 * ============================================================================
 *                            DESIGN GOALS
 * ============================================================================
 *
 * ✓ Deterministic real-time step generation
 * ✓ No cumulative frequency error using DDS (Direct Digital Synthesis)
 * ✓ Strict acceleration/deceleration control
 * ✓ Mechanical backlash compensation
 * ✓ Hardware efficiency via direct port access (minimal CPU overhead)
 *
 * ============================================================================
 *                          PIN CONFIGURATION
 * ============================================================================
 *
 * Usage: setPins(stepPin, dirPin, enablePin)
 *
 * Example Configuration:
 * ┌─────────────────────────────────────────────────────────┐
 * │ setPins(11, 8, 7);                                      │
 * │                                                         │
 * │  stepPin   (11)  → STEP pulse output   [to driver]      │
 * │  dirPin    (8)   → Direction signal    [to driver]      │
 * │  enablePin (7)   → Driver enable       [to driver]      │
 * └─────────────────────────────────────────────────────────┘
 *
 * ENABLE PIN LOGIC
 * ────────────────
 * The ENABLE pin is ACTIVE LOW:
 *   • LOW  (0V)  → Motor driver ENABLED
 *   • HIGH (5V)  → Motor driver DISABLED (standby)
 *
 * ============================================================================
 *                        SPEED CONVENTION
 * ============================================================================
 *
 * ⚠️  IMPORTANT: Speed is a SIGNED value
 *
 * Positive values (+):
 *   • Forward rotation
 *   • DIR pin → HIGH
 *
 * Negative values (-):
 *   • Reverse rotation
 *   • DIR pin → LOW
 *
 * The sign directly controls the motor direction.
 *
 * ============================================================================
 *                              UNITS
 * ============================================================================
 *
 * Speed        : steps/second (SIGNED)           [e.g., 1000.5]
 * Acceleration : steps/second²                   [e.g., 5000.0]
 * Backlash     : steps                           [e.g., 50]
 *
 * ============================================================================
 *                        INITIALIZATION ORDER (CRITICAL)
 * ============================================================================
 *
 * Must be called in this specific order for correct operation:
 *
 *   1️⃣  setPins()           → Configure hardware pins
 *   2️⃣  setAcceleration()   → Set acceleration ramp
 *   3️⃣  setBacklash()       → Configure backlash compensation
 *   4️⃣  setSpeed()          → Set target speed & enable motor
 *
 * Deviation from this order may result in undefined behavior.
 *
 * ============================================================================
 *                         USAGE EXAMPLE
 * ============================================================================
 *
 *   // Initialization
 *   AstroStepper::setPins(11, 8, 7);
 *   AstroStepper::setAcceleration(5000.0);
 *   AstroStepper::setBacklash(50, 100.0, 5000.0);
 *
 *   // Runtime
 *   AstroStepper::setSpeed(1000.0);    // Move forward at 1000 steps/sec
 *   AstroStepper::enable();             // Power motor
 *
 *   // ... later ...
 *   AstroStepper::setSpeed(-500.0);    // Move backward at 500 steps/sec
 *   AstroStepper::disable();            // Cut power
 *
 * ============================================================================
 */

class AstroStepper
{
public:

    // ========================================================================
    //                         CONFIGURATION
    // ========================================================================

    /**
     * Configure the hardware pins for stepper motor control.
     *
     * @param stepPin   Arduino pin for STEP pulses (typically PWM pin)
     * @param dirPin    Arduino pin for direction signal
     * @param enablePin Arduino pin for driver enable (active LOW)
     *
     * @note Must be called first during initialization
     * @note All pins must be unique
     */
    static void setPins(uint8_t stepPin, uint8_t dirPin, uint8_t enablePin);

    /**
     * Set the acceleration/deceleration rate.
     *
     * @param accel_steps_s2 Acceleration in steps/second²
     *                       Range: 0 to ~1,000,000 (typical: 1000-10000)
     *                       Higher values = faster ramp, more mechanical stress
     *                       Lower values = smoother motion, longer ramp time
     *
     * @note Must be called before setBacklash()
     * @note Affects both acceleration and deceleration equally
     */
    static void setAcceleration(float accel_steps_s2);

    /**
     * Configure mechanical backlash compensation.
     *
     * Backlash occurs when the motor direction changes and the gears/
     * mechanical system has slack. This function compensates by moving
     * slightly in the opposite direction before engaging forward motion.
     *
     * @param steps     Backlash distance in steps (0 to disable)
     * @param vmax      Maximum speed during backlash compensation (steps/sec)
     * @param accel     Acceleration during backlash compensation (steps/sec²)
     *
     * @note Must be called before setSpeed()
     * @note Set steps=0 to disable backlash compensation
     */
    static void setBacklash(int32_t steps, float vmax, float accel);

    // ========================================================================
    //                         SPEED CONTROL
    // ========================================================================

    /**
     * Set the target motor speed.
     *
     * @param speed_steps_s Target speed in steps/second (SIGNED)
     *                      Positive  → forward rotation
     *                      Negative  → reverse rotation
     *                      Zero      → motor coasting to stop
     *
     * @note Motor will accelerate smoothly toward target speed
     * @note Acceleration profile is determined by setAcceleration()
     */
    static void setSpeed(float speed_steps_s);

    /**
     * Get the target (commanded) speed.
     *
     * @return Target speed in steps/second (SIGNED)
     *
     * @note This is the commanded speed, not the actual current speed
     * @see getCurrentSpeed()
     */
    static float getTargetSpeed();

    /**
     * Get the actual current motor speed.
     *
     * @return Current speed in steps/second (SIGNED)
     *
     * @note This is the actual instantaneous speed based on acceleration profile
     * @note May differ from target speed during acceleration/deceleration phases
     * @see getTargetSpeed()
     */
    static float getCurrentSpeed();

    // ========================================================================
    //                         POWER MANAGEMENT
    // ========================================================================

    /**
     * Enable the stepper motor driver.
     *
     * @note Sets ENABLE pin LOW (active state)
     * @note Motor will begin executing the motion profile set by setSpeed()
     */
    static void enable();

    /**
     * Disable the stepper motor driver.
     *
     * @note Sets ENABLE pin HIGH (inactive state)
     * @note Motor enters standby/sleep mode, consuming minimal power
     * @note Does NOT change the target speed; call enable() to resume
     */
    static void disable();

    // ========================================================================
    //                       INTERRUPT HANDLER (INTERNAL)
    // ========================================================================

    /**
     * Interrupt service routine for step pulse generation.
     *
     * ⚠️  DO NOT call this function manually!
     *
     * This must be called by an external timer interrupt (typically Timer1 or
     * Timer2 on Arduino) at high frequency (~10-20 kHz recommended).
     *
     * Example setup (external):
     *   attachInterrupt(digitalPinToInterrupt(2), AstroStepper::isrHandler, RISING);
     *
     * @note Real-time critical code - must execute quickly
     * @note Manages DDS accumulator and pulse generation
     * @note Do not modify unless implementing custom interrupt handling
     */
    static void isrHandler();
};

#endif
