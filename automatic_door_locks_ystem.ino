/*
  =====================================================
   AUTOMATIC DOOR LOCK SYSTEM (with I2C LCD + Lockout)
   Components: Arduino Uno, 4x4 Matrix Keypad,
               SG90 Servo Motor, Red LED, Green LED,
               Buzzer, Push Button (Exit Switch),
               16x2 I2C LCD Display

    FEATURE:
   - 3 wrong password attempts -> system locks out
     for 40 seconds
   - During lockout: RED LED glows, keypad ignored
   - When lockout timer finishes (resets): GREEN LED
     glows to show system is ready again

     Pin Connections
Component	Pin	Connects to Uno
Keypad	R1, R2, R3, R4	D2, D3, D4, D5
Keypad	C1, C2, C3, C4	D6, D7, D8, A0
I2C LCD	GND	GND
I2C LCD	VCC	5V
I2C LCD	SDA	A4
I2C LCD	SCL	A5
Servo	Signal	D9 (PWM)
Servo	VCC / GND	5V / GND
Green LED	Anode (+)	D11 (via 220Ω resistor)
Red LED	Anode (+)	D12 (via 220Ω resistor)
Buzzer	+	D10
Exit Button	One leg	D13 (other leg to GND)

   Libraries required (Install via Library Manager):
   1. Keypad by Mark Stanley, Alexander Brevig
   2. LiquidCrystal I2C by Frank de Brabander
   3. Servo (comes pre-installed with Arduino IDE)
   4. Wire (comes pre-installed with Arduino IDE)
  =====================================================

  Working 

  1. Idle state — the Arduino just sits there. Red LED on, servo at 0°, LCD says "Door locked."

2. Reading the keypad — every loop cycle, keypad.getKey() checks if a button was just pressed.
 If yes, that character (like '1') gets added onto a growing text string called inputCode.

3. Pressing # — this is the trigger that tells the code "stop collecting, go check what I typed."
 Without it, the code has no clean way to know the person is finished.

4. Comparing passwords — the code does one simple check: if (inputCode == password).
 That's literally comparing the string you typed against "1234".

5a. If it matches — unlockDoor() runs: servo rotates open, green LED turns on, a beep confirms it, and 
after 5 seconds it locks itself again automatically.

5b. If it doesn't match — wrongAttempts goes up by 1, red LED blinks, buzzer beeps as a warning,
 and the LCD shows how many tries are left.

6. Three wrong tries in a row — wrongAttempts >= 3 becomes true,
 so lockoutSystem() takes over: red LED stays solid, the keypad is ignored,
  and the LCD counts down from 40 to 0. When it hits zero, the counter resets to 0 and
   the system flashes green briefly to say "I'm listening again."
*/

#include <Keypad.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- LCD CONFIGURATION ----------------
// If screen stays blank, change 0x27 to 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- KEYPAD CONFIGURATION ----------------
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {2, 3, 4, 5};    // Keypad R1, R2, R3, R4
byte colPins[COLS] = {6, 7, 8, A0};   // Keypad C1, C2, C3, C4

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------------- SERVO CONFIGURATION ----------------
Servo doorServo;
const int servoPin     = 9;
const int LOCK_ANGLE   = 0;
const int UNLOCK_ANGLE = 90;

// ---------------- OTHER COMPONENTS ----------------
const int greenLED   = 11;
const int redLED     = 12;
const int buzzer     = 10;
const int exitButton = 13;

// ---------------- PASSWORD SETTINGS ----------------
String password  = "1234";   // <-- Change your password here
String inputCode = "";

// ---------------- LOCKOUT SETTINGS ----------------
const int MAX_ATTEMPTS = 3;              // wrong tries allowed
const unsigned long LOCKOUT_TIME = 40;   // lockout time in seconds
int wrongAttempts = 0;

void setup() {
  Serial.begin(9600);
  Serial.println("=====================================");
  Serial.println("   AUTOMATIC DOOR LOCK SYSTEM BOOTING ");
  Serial.println("=====================================");

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Door Lock Sys");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1500);

  doorServo.attach(servoPin);

  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(exitButton, INPUT_PULLUP);

  lockDoor();
  showReadyScreen();

  Serial.println("System Ready. Enter 4-digit password + '#'");
}

void loop() {
  char key = keypad.getKey();

  // ---- Exit button unlocks instantly, works even mid-normal-state ----
  if (digitalRead(exitButton) == LOW) {
    Serial.println("[Exit Button] Pressed -> Unlocking door from inside");
    unlockDoor();
    delay(500); // debounce
  }

  if (key) {
    Serial.print("Key Pressed: ");
    Serial.println(key);

    if (key == '#') {
      checkPassword();
    }
    else if (key == '*') {
      inputCode = "";
      Serial.println("Input Cleared");
      lcd.setCursor(0, 1);
      lcd.print("                "); // clear line
      lcd.setCursor(0, 1);
      lcd.print("Enter Password:");
    }
    else {
      inputCode += key;
      Serial.print("Current Input: ");
      Serial.println(inputCode);

      lcd.setCursor(0, 1);
      lcd.print("Code: ");
      lcd.print(inputCode);

      if (inputCode.length() >= password.length()) {
        checkPassword();
      }
    }
  }
}

// ---------------- FUNCTION: Check entered password ----------------
void checkPassword() {
  if (inputCode == password) {
    Serial.println(">>> Access Granted! <<<");
    wrongAttempts = 0;   // reset counter on success
    unlockDoor();
  } else {
    wrongAttempts++;
    Serial.print(">>> Access Denied! Wrong Attempt #");
    Serial.println(wrongAttempts);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Access Denied!");
    lcd.setCursor(0, 1);
    lcd.print("Tries left: ");
    lcd.print(MAX_ATTEMPTS - wrongAttempts);

    wrongAlert();

    if (wrongAttempts >= MAX_ATTEMPTS) {
      lockoutSystem();
    } else {
      delay(1500);
      showReadyScreen();
    }
  }
  inputCode = "";
}

// ---------------- FUNCTION: Unlock the door ----------------
void unlockDoor() {
  doorServo.write(UNLOCK_ANGLE);
  digitalWrite(greenLED, HIGH);
  digitalWrite(redLED, LOW);
  tone(buzzer, 1000, 200);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Access Granted");
  lcd.setCursor(0, 1);
  lcd.print("Door Unlocked");

  Serial.println("Door UNLOCKED. Auto-locking in 5 seconds...");
  delay(5000);
  lockDoor();
  showReadyScreen();
}

// ---------------- FUNCTION: Lock the door ----------------
void lockDoor() {
  doorServo.write(LOCK_ANGLE);
  digitalWrite(greenLED, LOW);
  digitalWrite(redLED, HIGH);
  Serial.println("Door LOCKED.");
}

// ---------------- FUNCTION: Wrong password beep/blink ----------------
void wrongAlert() {
  Serial.println("Sounding alarm...");
  for (int i = 0; i < 3; i++) {
    digitalWrite(redLED, HIGH);
    tone(buzzer, 2000, 150);
    delay(200);
    digitalWrite(redLED, LOW);
    delay(200);
  }
  digitalWrite(redLED, HIGH);
}

// ---------------- FUNCTION: Lockout after 3 wrong attempts ----------------
void lockoutSystem() {
  Serial.println("!!! 3 WRONG ATTEMPTS - SYSTEM LOCKED OUT !!!");

  // Long alarm beep
  tone(buzzer, 1500, 1000);

  // RED LED glows solid during lockout
  digitalWrite(greenLED, LOW);
  digitalWrite(redLED, HIGH);

  // Countdown timer shown on LCD and Serial Monitor
  for (int t = LOCKOUT_TIME; t > 0; t--) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SYSTEM LOCKED!");
    lcd.setCursor(0, 1);
    lcd.print("Wait: ");
    lcd.print(t);
    lcd.print(" sec");

    Serial.print("Lockout time remaining: ");
    Serial.print(t);
    Serial.println(" sec");

    delay(1000);
  }

  // ---- Timer finished / reset ----
  wrongAttempts = 0;             // reset wrong attempt counter
  digitalWrite(redLED, LOW);
  digitalWrite(greenLED, HIGH);  // GREEN LED glows -> system ready again
  tone(buzzer, 1000, 200);       // short beep to signal reset

  Serial.println("Lockout timer reset. System Ready again.");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Reset!");
  lcd.setCursor(0, 1);
  lcd.print("Ready to use");
  delay(1500);

  digitalWrite(greenLED, LOW);   // back to normal locked state
  digitalWrite(redLED, HIGH);
  showReadyScreen();
}

// ---------------- FUNCTION: Show default ready screen ----------------
void showReadyScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Door Locked");
  lcd.setCursor(0, 1);
  lcd.print("Enter Password:");
}