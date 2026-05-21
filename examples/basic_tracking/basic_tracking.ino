/*
 * ============================================================================
 *                      ASTROSTEPPER - BASIC TRACKING EXAMPLE
 * ============================================================================
 *
 * This example demonstrates the core functionality of the AstroStepper library:
 *   • Motor initialization and configuration
 *   • Speed control with smooth acceleration
 *   • Direction reversal and backlash compensation
 *   • Enable/disable power management
 *
 * HARDWARE REQUIREMENTS:
 *   • Arduino AVR board (Uno, Mega, Nano, etc.)
 *   • Stepper motor and compatible driver (STEP/DIR/ENABLE compatible)
 *   • Pin configuration (adjustable below):
 *       - STEP signal → Arduino pin 11
 *       - DIR signal  → Arduino pin 8
 *       - ENABLE      → Arduino pin 7 (active LOW)
 *
 * COMPILATION & UPLOAD:
 *   1. Open this file in Arduino IDE
 *   2. Install AstroStepper library (Sketch → Include Library → Manage Libraries)
 *   3. Select your Arduino board and COM port
 *   4. Click Upload
 *   5. Open Serial Monitor (115200 baud) to see debug output
 *
 * ============================================================================
 */

#include <AstroStepper.h>

/*
 * ============================================================================
 *                           PIN CONFIGURATION
 * ============================================================================
 *
 * Define which Arduino pins control the stepper motor driver.
 * These must be unique and match your hardware connections.
 *
 * STEP PIN (11):
 *   - Generates pulse train commanding motor steps
 *   - One rising edge = one motor step
 *   - Frequency controlled by speed (steps/second)
 *
 * DIR PIN (8):
 *   - Controls motor rotation direction
 *   - HIGH (5V)  = forward rotation (positive speed)
 *   - LOW  (0V)  = reverse rotation (negative speed)
 *
 * ENABLE PIN (7):
 *   - Controls motor power delivery
 *   - LOW  (0V)  = motor enabled (receiving power)
 *   - HIGH (5V)  = motor disabled (standby mode)
 *   - NOTE: This is ACTIVE LOW (opposite of typical GPIO logic)
 */
const uint8_t STEP_PIN   = 11;      // STEP signal → driver
const uint8_t DIR_PIN    = 8;       // DIR signal → driver
const uint8_t ENABLE_PIN = 7;       // ENABLE signal → driver


/*
 * ============================================================================
 *                        MOTOR CONTROL PARAMETERS
 * ============================================================================
 *
 * Tunable parameters for your specific motor and mechanical system.
 * Adjust these based on your hardware and application requirements.
 */

// Acceleration rate during ramp-up and ramp-down
// Unit: steps/second²
// Range: typical 100-50000 (higher = faster response, more mechanical stress)
// For smooth astronomical tracking: use 100-5000
// For rapid positioning: use 10000-50000
const float ACCELERATION = 100.0;

// Backlash compensation distance (mechanical slack in gears)
// Unit: motor steps
// Range: 0 (disabled) to 200+ (very loose mechanics)
// Typical values:
//   - Tight mount: 10-20 steps
//   - Normal mount: 30-80 steps
//   - Loose mount: 100+ steps
// Measure manually by rotating slowly in each direction and noting the offset
const int32_t BACKLASH_STEPS = 40;

// Maximum speed during backlash compensation phase
// Unit: steps/second
// Should be much lower than normal operating speed to ensure gentle motion
// Typical: 50-200 steps/sec
// Too high: jerky compensation, defeats the purpose
// Too low: takes too long to complete compensation
const float BACKLASH_VMAX = 50.0;

// Acceleration during backlash compensation phase
// Unit: steps/second²
// Should be smoother than main acceleration to prevent mechanical shock
// Typical: 1/5 to 1/10 of main acceleration
// Example: if main accel is 5000, use 500-1000 here
const float BACKLASH_ACCEL = 500.0;

// Demo speed profiles - modify these to test different behaviors
const float SPEED_SLOW    = 200.0;   // Slow tracking (steps/second)
const float SPEED_MEDIUM  = 500.0;   // Medium speed
const float SPEED_FAST    = 1000.0;  // Fast speed
const float SPEED_REVERSE = -1000.0; // Reverse direction (negative = backward)


/*
 * ============================================================================
 *                        SEQUENCE TIMING CONSTANTS
 * ============================================================================
 *
 * Delay durations for demonstration phases (in milliseconds).
 * Increase these for slower observation, decrease for faster testing.
 */
const unsigned long DELAY_RAMP_UP   = 3000;  // Hold at each speed (ramp phase)
const unsigned long DELAY_REVERSE   = 4000;  // Extended time to see reverse motion
const unsigned long DELAY_STOP      = 2000;  // Pause at stop
const unsigned long DELAY_SEQUENCE  = 5000;  // Loop cycle timing


/*
 * ============================================================================
 *                          SETUP FUNCTION
 * ============================================================================
 *
 * Called once at startup. Initializes the serial port and configures the
 * stepper motor controller.
 *
 * CRITICAL INITIALIZATION ORDER:
 *   1. setPins()           - Configure hardware pins
 *   2. setAcceleration()   - Set ramp rate
 *   3. setBacklash()       - Configure backlash compensation
 *   4. setSpeed()          - Start motion
 *
 * Any deviation from this order will cause undefined behavior!
 */
void setup()
{
    // Initialize serial communication for debugging output
    Serial.begin(115200);
    delay(500);  // Wait for serial to stabilize
    
    Serial.println("=====================================");
    Serial.println("   ASTROSTEPPER - BASIC TRACKING");
    Serial.println("=====================================");
    Serial.println();
    
    // =========================================================================
    // STEP 1: Configure hardware pins (MUST BE FIRST)
    // =========================================================================
    Serial.println("[1/4] Configuring pins...");
    Serial.print("  STEP pin  : ");
    Serial.println(STEP_PIN);
    Serial.print("  DIR pin   : ");
    Serial.println(DIR_PIN);
    Serial.print("  ENABLE pin: ");
    Serial.println(ENABLE_PIN);
    
    AstroStepper::setPins(STEP_PIN, DIR_PIN, ENABLE_PIN);
    Serial.println("      ✓ Pins configured\n");
    
    // =========================================================================
    // STEP 2: Set acceleration/deceleration rate
    // =========================================================================
    Serial.println("[2/4] Setting acceleration profile...");
    Serial.print("  Acceleration: ");
    Serial.print(ACCELERATION);
    Serial.println(" steps/s²");
    
    AstroStepper::setAcceleration(ACCELERATION);
    Serial.println("      ✓ Acceleration set\n");
    
    // =========================================================================
    // STEP 3: Configure backlash compensation
    // =========================================================================
    Serial.println("[3/4] Configuring backlash compensation...");
    Serial.print("  Backlash distance: ");
    Serial.print(BACKLASH_STEPS);
    Serial.println(" steps");
    Serial.print("  Compensation speed: ");
    Serial.print(BACKLASH_VMAX);
    Serial.println(" steps/s");
    Serial.print("  Compensation accel: ");
    Serial.print(BACKLASH_ACCEL);
    Serial.println(" steps/s²");
    
    AstroStepper::setBacklash(BACKLASH_STEPS, BACKLASH_VMAX, BACKLASH_ACCEL);
    Serial.println("      ✓ Backlash compensation configured\n");
    
    // =========================================================================
    // Initialization complete - ready to demonstrate sequences
    // =========================================================================
    Serial.println("[4/4] Initialization sequence starting...\n");
    
    // --- SEQUENCE PHASE 1: Forward slow ramp ---
    Serial.println(">>> PHASE 1: Slow forward motion");
    Serial.print("    Target speed: ");
    Serial.print(SPEED_SLOW);
    Serial.println(" steps/s");
    Serial.println("    Starting motor at slow speed...");
    
    AstroStepper::setSpeed(SPEED_SLOW);
    Serial.println("    Motor accelerating...");
    
    delay(DELAY_RAMP_UP);
    Serial.print("    Current speed: ");
    Serial.print(AstroStepper::getCurrentSpeed());
    Serial.println(" steps/s\n");
    
    // --- SEQUENCE PHASE 2: Increase speed ---
    Serial.println(">>> PHASE 2: Speed increase (acceleration visible)");
    Serial.print("    Target speed: ");
    Serial.print(SPEED_FAST);
    Serial.println(" steps/s");
    Serial.println("    Accelerating to higher speed...");
    
    AstroStepper::setSpeed(SPEED_FAST);
    
    delay(DELAY_RAMP_UP);
    Serial.print("    Current speed: ");
    Serial.print(AstroStepper::getCurrentSpeed());
    Serial.println(" steps/s\n");
    
    // --- SEQUENCE PHASE 3: Reverse direction (triggers backlash) ---
    Serial.println(">>> PHASE 3: Reverse direction (backlash compensation)");
    Serial.print("    Target speed: ");
    Serial.print(SPEED_REVERSE);
    Serial.println(" steps/s");
    Serial.println("    Reversing motor - backlash compensation activated!");
    Serial.print("    Compensation will take ~");
    Serial.print((float)BACKLASH_STEPS / BACKLASH_VMAX);
    Serial.println(" seconds...");
    
    AstroStepper::setSpeed(SPEED_REVERSE);
    
    delay(DELAY_REVERSE);
    Serial.print("    Current speed: ");
    Serial.print(AstroStepper::getCurrentSpeed());
    Serial.println(" steps/s\n");
    
    // --- SEQUENCE PHASE 4: Reverse slow ---
    Serial.println(">>> PHASE 4: Slow reverse motion");
    Serial.print("    Target speed: ");
    Serial.print(-SPEED_MEDIUM);
    Serial.println(" steps/s");
    Serial.println("    Decelerating to slower reverse speed...");
    
    AstroStepper::setSpeed(-SPEED_MEDIUM);
    
    delay(DELAY_RAMP_UP);
    Serial.print("    Current speed: ");
    Serial.print(AstroStepper::getCurrentSpeed());
    Serial.println(" steps/s\n");
    
    // --- SEQUENCE PHASE 5: Stop ---
    Serial.println(">>> PHASE 5: Stop and coast");
    Serial.println("    Commanding stop (motor coasts to stop)...");
    
    AstroStepper::setSpeed(0.0);
    
    delay(DELAY_STOP);
    Serial.print("    Current speed: ");
    Serial.print(AstroStepper::getCurrentSpeed());
    Serial.println(" steps/s\n");
    
    // --- SEQUENCE PHASE 6: Restart forward ---
    Serial.println(">>> PHASE 6: Restart forward motion");
    Serial.print("    Target speed: ");
    Serial.print(SPEED_FAST - 200);
    Serial.println(" steps/s");
    Serial.println("    Restarting motor...");
    
    AstroStepper::setSpeed(SPEED_FAST - 200);
    
    Serial.println("\n=====================================");
    Serial.println("   SETUP COMPLETE - ENTERING LOOP");
    Serial.println("=====================================\n");
}


/*
 * ============================================================================
 *                          MAIN LOOP FUNCTION
 * ============================================================================
 *
 * Called repeatedly after setup(). Demonstrates continuous alternation
 * between forward and reverse motion with backlash compensation.
 *
 * The motor operation is completely driven by the interrupt service routine
 * (ISR) running at 16 kHz, so minimal work is needed here. This loop simply
 * demonstrates periodic speed changes.
 *
 * PERFORMANCE NOTES:
 *   • ISR runs independently in background (16 kHz timer interrupt)
 *   • setSpeed() is non-blocking and thread-safe
 *   • Motor continues moving even while loop is executing
 *   • Can call getTargetSpeed() or getCurrentSpeed() for monitoring
 */
void loop()
{
    // === Continuous alternating motion sequence ===
    
    // Forward motion phase
    Serial.println(">>> LOOP: Forward fast");
    Serial.print("    Target: ");
    Serial.print(SPEED_FAST);
    Serial.println(" steps/s");
    
    AstroStepper::setSpeed(SPEED_FAST);
    
    // Hold forward motion for specified duration
    delay(DELAY_SEQUENCE);
    
    // Display current speed at end of phase
    Serial.print("    Final speed: ");
    Serial.print(AstroStepper::getCurrentSpeed());
    Serial.println(" steps/s\n");
    
    // Reverse motion phase
    Serial.println(">>> LOOP: Reverse fast (backlash compensation triggers)");
    Serial.print("    Target: ");
    Serial.print(SPEED_REVERSE);
    Serial.println(" steps/s");
    
    AstroStepper::setSpeed(SPEED_REVERSE);
    
    // Hold reverse motion for specified duration
    delay(DELAY_SEQUENCE);
    
    // Display current speed at end of phase
    Serial.print("    Final speed: ");
    Serial.print(AstroStepper::getCurrentSpeed());
    Serial.println(" steps/s\n");
    
    // Optional: Add Serial command processing here for interactive control
    // Example:
    //   if (Serial.available()) {
    //     char cmd = Serial.read();
    //     if (cmd == '+') AstroStepper::setSpeed(AstroStepper::getTargetSpeed() * 1.1);
    //     if (cmd == '-') AstroStepper::setSpeed(AstroStepper::getTargetSpeed() / 1.1);
    //     if (cmd == 's') AstroStepper::setSpeed(0);
    //   }
}


/*
 * ============================================================================
 *                          OPTIONAL FUNCTIONS
 * ============================================================================
 *
 * Uncomment and use these helper functions to extend the example.
 */

/*
 * Monitor motor status and print to serial
 * Call periodically from loop() if desired
 */
/*
void print_motor_status() {
    float target = AstroStepper::getTargetSpeed();
    float current = AstroStepper::getCurrentSpeed();
    float error = abs(current - target);
    
    Serial.print("Target: ");
    Serial.print(target);
    Serial.print(" steps/s | Current: ");
    Serial.print(current);
    Serial.print(" steps/s | Error: ");
    Serial.print(error);
    Serial.println(" steps/s");
}
*/

/*
 * Enable interactive serial control
 * Add to loop() for user command interface
 */
/*
void handle_serial_commands() {
    if (Serial.available()) {
        char cmd = Serial.read();
        Serial.println(cmd);  // Echo command
        
        switch (cmd) {
            case '+':
                // Increase speed by 10%
                {
                    float speed = AstroStepper::getTargetSpeed();
                    AstroStepper::setSpeed(speed * 1.1);
                    Serial.println("Speed increased");
                }
                break;
                
            case '-':
                // Decrease speed by 10%
                {
                    float speed = AstroStepper::getTargetSpeed();
                    AstroStepper::setSpeed(speed / 1.1);
                    Serial.println("Speed decreased");
                }
                break;
                
            case 's':
                // Stop
                AstroStepper::setSpeed(0);
                Serial.println("Stopping...");
                break;
                
            case 'r':
                // Reverse
                {
                    float speed = AstroStepper::getTargetSpeed();
                    AstroStepper::setSpeed(-speed);
                    Serial.println("Reversing");
                }
                break;
        }
    }
}
*/

/*
 * ============================================================================
 *                            END OF EXAMPLE
 * ============================================================================
 *
 * For more advanced examples, see:
 *   - examples/advanced_tracking/ - Astronomical mount simulation
 *   - Documentation in USAGE.txt for complete API reference
 *   - Source code in src/AstroStepper.h for implementation details
 *
 * Questions or issues?
 *   → Check GitHub: https://github.com/nicoviteau/AstroStepper
 *   → Review USAGE.txt for troubleshooting
 *
 */
