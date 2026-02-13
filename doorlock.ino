#include <Keypad.h>
#include <EEPROM.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>

// ================= CONFIG =================
#define FP_RX 16
#define FP_TX 17
#define FP_BAUD 57600

#define OWNER_COUNT_ADDR 10
#define DOOR_LED 4

#define HOLD_TIME_MS 3000     // 3 sec hold
#define TIMEOUT_MS   10000    // 10 sec timeout
// =========================================

HardwareSerial FingerSerial(2);
Adafruit_Fingerprint finger(&FingerSerial);

// ================= KEYPAD =================
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {32, 33, 25, 26};
byte colPins[COLS] = {27, 14, 12, 13};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= PASSWORD =================
String password = "";
String input = "";
String adminInput = "";
String newPass = "";

const String adminKey = "1432";

// ================= STATES =================
bool enrollMode = false;
bool resetMode = false;
bool enteringNewPass = false;
bool confirmingPass = false;
bool holdLock = false;
bool normalEntryActive = false;

unsigned long normalIdleTimer = 0;
unsigned long enrollIdleTimer = 0;
unsigned long resetIdleTimer  = 0;

// ================= DOOR =================
bool doorLedActive = false;
unsigned long doorLedStartTime = 0;

// ================= DECLARATIONS =================
void keypadEvent(KeypadEvent key);
bool enrollNewOwner();
void checkFingerprint();
void showPrompt();
void exitEnrollMode();
void exitResetMode();

// ================= EEPROM =================
void savePassword(String pass) {
  for (int i = 0; i < 6; i++) EEPROM.write(i, pass[i]);
  EEPROM.commit();
}

String readPassword() {
  String pass = "";
  for (int i = 0; i < 6; i++) pass += char(EEPROM.read(i));
  return pass;
}

// ================= PROMPT =================
void showPrompt() {
  Serial.println();
  Serial.println("👉 Enter 6-digit password or use fingerprint");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(64);

  pinMode(DOOR_LED, OUTPUT);
  digitalWrite(DOOR_LED, LOW);

  keypad.addEventListener(keypadEvent);
  keypad.setHoldTime(HOLD_TIME_MS);

  password = readPassword();
  if (password.length() != 6) {
    password = "654321";
    savePassword(password);
  }

  FingerSerial.begin(FP_BAUD, SERIAL_8N1, FP_RX, FP_TX);
  if (!finger.verifyPassword()) {
    Serial.println("❌ Fingerprint sensor not detected");
    while (1);
  }

  if (EEPROM.read(OWNER_COUNT_ADDR) == 0) {
    Serial.println("🔑 No owners found – enroll first owner");
    enrollNewOwner();
  }

  Serial.println("🔐 Door Lock Ready");
  showPrompt();
}

// ================= LOOP =================
void loop() {

  checkFingerprint();

  // Door auto-off
  if (doorLedActive && millis() - doorLedStartTime >= 5000) {
    doorLedActive = false;
    digitalWrite(DOOR_LED, LOW);
  }

  // ---------- TIMEOUTS ----------
  if (normalEntryActive && millis() - normalIdleTimer >= TIMEOUT_MS) {
    input = "";
    normalEntryActive = false;
    Serial.println("\n⏱ Timeout");
    showPrompt();
    return;
  }

  if (enrollMode && millis() - enrollIdleTimer >= TIMEOUT_MS) {
    Serial.println("\n⏱ Enrollment timeout");
    exitEnrollMode();
    return;
  }

  if (resetMode && millis() - resetIdleTimer >= TIMEOUT_MS) {
    Serial.println("\n⏱ Reset timeout");
    exitResetMode();
    return;
  }

  char key = keypad.getKey();
  if (!key) return;

  // ================= RESET MODE =================
  if (resetMode) {
    resetIdleTimer = millis();

    if (key >= '0' && key <= '9') {
      input += key;
      Serial.print("*");
    }

    if (!enteringNewPass && input.length() == 4) {
      Serial.println();
      if (input == adminKey) {
        input = "";
        enteringNewPass = true;
        Serial.println("Enter NEW password:");
      } else {
        Serial.println("❌ Wrong reset key");
        exitResetMode();
      }
    }
    else if (enteringNewPass && !confirmingPass && input.length() == 6) {
      newPass = input;
      input = "";
      confirmingPass = true;
      Serial.println("Confirm NEW password:");
    }
    else if (confirmingPass && input.length() == 6) {
      Serial.println();
      if (input == newPass) {
        password = newPass;
        savePassword(password);
        Serial.println("✅ Password changed SUCCESSFULLY");
      } else {
        Serial.println("❌ Password mismatch");
      }
      exitResetMode();
    }
    return;
  }

  // ================= ENROLL MODE =================
  if (enrollMode) {
    enrollIdleTimer = millis();

    if (key >= '0' && key <= '9') {
      adminInput += key;
      Serial.print("*");
    }

    if (adminInput.length() == 4) {
      Serial.println();
      if (adminInput == adminKey) {
        if (!enrollNewOwner()) Serial.println("❌ Enrollment failed");
      } else {
        Serial.println("❌ Wrong admin key");
      }
      exitEnrollMode();
    }
    return;
  }

  // ================= NORMAL MODE =================
  if (key >= '0' && key <= '9') {
    normalEntryActive = true;
    normalIdleTimer = millis();
    input += key;
    Serial.print("*");
  }

  if (input.length() == 6) {
    Serial.println();
    if (input == password) {
      Serial.println("✅ ACCESS GRANTED");
      digitalWrite(DOOR_LED, HIGH);
      doorLedActive = true;
      doorLedStartTime = millis();
    } else {
      Serial.println("❌ ACCESS DENIED");
    }
    input = "";
    normalEntryActive = false;
    showPrompt();
  }
}

// ================= EXIT FUNCTIONS =================
void exitEnrollMode() {
  enrollMode = false;
  holdLock = true;
  adminInput = "";
  input = "";
  normalEntryActive = false;
  showPrompt();
}

void exitResetMode() {
  resetMode = false;
  enteringNewPass = false;
  confirmingPass = false;
  input = "";
  showPrompt();
}

// ================= ENROLL OWNER =================
bool enrollNewOwner() {
  unsigned long stepTimer = millis();
  uint8_t newID = EEPROM.read(OWNER_COUNT_ADDR) + 1;

  Serial.print("👤 Enrolling OWNER ID ");
  Serial.println(newID);

  int p = -1;
  Serial.println("Place finger");

  while (p != FINGERPRINT_OK) {
    if (millis() - stepTimer >= TIMEOUT_MS) return false;
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) continue;
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) return false;

  Serial.println("Remove finger");
  delay(2000);
  stepTimer = millis();

  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    if (millis() - stepTimer >= TIMEOUT_MS) return false;
  }

  Serial.println("Place SAME finger again");
  stepTimer = millis();
  p = -1;

  while (p != FINGERPRINT_OK) {
    if (millis() - stepTimer >= TIMEOUT_MS) return false;
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) continue;
  }

  if (finger.image2Tz(2) != FINGERPRINT_OK) return false;
  if (finger.createModel() != FINGERPRINT_OK) return false;
  if (finger.storeModel(newID) != FINGERPRINT_OK) return false;

  EEPROM.write(OWNER_COUNT_ADDR, newID);
  EEPROM.commit();

  Serial.println("✅ New owner enrolled SUCCESSFULLY");
  return true;
}

// ================= CHECK FP =================
void checkFingerprint() {
  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz() != FINGERPRINT_OK) return;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return;

  uint8_t id = finger.fingerID;
  uint8_t count = EEPROM.read(OWNER_COUNT_ADDR);

  if (id >= 1 && id <= count) {
    Serial.println("🖐️ Fingerprint ACCESS GRANTED");
    digitalWrite(DOOR_LED, HIGH);
    doorLedActive = true;
    doorLedStartTime = millis();
    showPrompt();
  }
}

// ================= HOLD EVENT =================
void keypadEvent(KeypadEvent key) {

  if (keypad.getState() == RELEASED) holdLock = false;

  // ADD OWNER → HOLD 1 or 2
  if (keypad.getState() == HOLD && !holdLock && !enrollMode && !resetMode) {
    if (key == '1' || key == '2') {
      enrollMode = true;
      enrollIdleTimer = millis();
      adminInput = "";
      Serial.println("\n🔐 ADD NEW OWNER MODE");
      Serial.println("Enter admin key:");
    }
  }

  // RESET PASSWORD → HOLD #
  if (keypad.getState() == HOLD && !resetMode && key == '#') {
    resetMode = true;
    resetIdleTimer = millis();
    input = "";
    Serial.println("\n🔄 RESET PASSWORD MODE");
    Serial.println("Enter reset key:");
  }
}
