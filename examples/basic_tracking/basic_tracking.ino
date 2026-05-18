#include <AstroStepper.h>
/* 
PIN MAPPING 
---------- 
STEP → pin 11 
DIR → pin 8 
ENABLE → pin 7 (ACTIVE LOW) 
UNITS 
----- 
Speed → steps per second (signed) 
Acceleration → steps per second² 
Backlash → steps 
*/ 
void setup() 
{ 
 AstroStepper::setPins(11,8,7); // STEP=11, DIR=8, ENABLE=7 
 AstroStepper::setAcceleration(100); // 100 steps/s² 
 AstroStepper::setBacklash(40); // 40 steps compensation 
 // --- Sequence demonstration --- 
 // Forward slow ramp 
 AstroStepper::setSpeed(200); // 200 steps/s 
 delay(3000); 
 // Increase speed (acceleration visible) 
 AstroStepper::setSpeed(1000); // 1000 steps/s 
 delay(3000); 
 // Reverse (triggers backlash compensation) 
 AstroStepper::setSpeed(-1000); // -1000 steps/s 
 delay(4000); 
 // Slow reverse 
 AstroStepper::setSpeed(-300); // -300 steps/s 
 delay(3000); 
 // Stop then restart forward 
 AstroStepper::setSpeed(0); 
 delay(2000); 
 AstroStepper::setSpeed(800); // 800 steps/s 
} 
void loop() 
{ 
 // Continuous alternation 
 AstroStepper::setSpeed(1200); 
 delay(5000); 
 AstroStepper::setSpeed(-1200); 
 delay(5000); 
}
