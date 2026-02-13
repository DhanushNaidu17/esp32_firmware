#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <ThingSpeak.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>



// ================= WIFI + THINGSPEAK =================
const char* ssid = "MitronTech_4G";
const char* wifiPassword = "Mitron&123";

const char* firmwareUrl = "http://yourserver.com/firmware.bin";

unsigned long channelID = 3261082;
const char* writeAPIKey = "UBXVN7WD7BQ6E0MM";

WiFiClient client;
unsigned long lastTSUpdate = 0;

// ===== ThingSpeak Queue System =====
bool pendingUpdate = false;
int pendingDoorStatus = 0;
int pendingAccessType = 0;


void sendToThingSpeak(int doorStatus, int accessType) {

  unsigned long now = millis();

  if (now - lastTSUpdate < 15000) {
    Serial.println("⏳ Waiting for ThingSpeak interval...");
    return;
  }

  ThingSpeak.setField(1, doorStatus);

  // Only update access type when valid
  if (accessType > 0) {
    ThingSpeak.setField(2, accessType);
  }

  int response = ThingSpeak.writeFields(channelID, writeAPIKey);

  if (response == 200) {
    Serial.println("📡 Data sent to ThingSpeak");
    lastTSUpdate = now;
  }
}
// ================= OTA SETUP =================

// ================= CONFIG =================
#define OTA_SAFE_PIN 15

#define FP_RX 16
#define FP_TX 17
#define FP_BAUD 57600

#define BT_RX 18
#define BT_TX 19

#define OWNER_COUNT_ADDR 10
#define DOOR_LED 4

#define HOLD_TIME_MS 3000
#define TIMEOUT_MS   10000
// =========================================

// ================= SERIAL =================
HardwareSerial FingerSerial(2);
HardwareSerial BTSerial(1);

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
bool normalEntryActive = false;

unsigned long modeTimer = 0;

// ================= DOOR =================
bool doorActive = false;
unsigned long doorStartTime = 0;
unsigned long doorDuration = 15000;   // 15 seconds

unsigned long lastFingerCheck = 0;
const unsigned long fingerInterval = 200;   // check every 200ms

// ================= DECLARATIONS =================
void keypadEvent(KeypadEvent key);
bool enrollNewOwner();
void checkFingerprint();
void showPrompt();
void exitToMain();

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
  Serial.println("\n👉 Enter password / fingerprint / Bluetooth OPEN");
}

// ================= EXIT =================
void exitToMain() {
  enrollMode = false;
  resetMode = false;
  enteringNewPass = false;
  confirmingPass = false;
  normalEntryActive = false;

  input = "";
  adminInput = "";
  newPass = "";

  Serial.println("\n⏱ Timeout - Returning to Main");
  showPrompt();
}
void checkForOTAUpdate() {

  Serial.println("🔄 Checking for OTA update...");

  t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);

  switch (ret) {

    case HTTP_UPDATE_FAILED:
      Serial.printf("❌ Update failed Error (%d): %s\n",
        httpUpdate.getLastError(),
        httpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("No updates available.");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("✅ Update successful. Rebooting...");
      break;
  }
}

// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  pinMode(OTA_SAFE_PIN, INPUT_PULLUP);

  bool otaSafeMode = (digitalRead(OTA_SAFE_PIN) == LOW);


  WiFi.begin(ssid, wifiPassword);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  ThingSpeak.begin(client);
  
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());


  if (otaSafeMode) {
  Serial.println("🚀 OTA SAFE MODE ACTIVE");
  while (true) {
    ArduinoOTA.handle();
    delay(10);
  }
}

  // ===== OTA SETUP =====
ArduinoOTA.setHostname("ESP32_DoorLock");
ArduinoOTA.setPassword("1234");

ArduinoOTA.onStart([]() {
  Serial.println("OTA Start");
});

ArduinoOTA.onEnd([]() {
  Serial.println("OTA End");
});

ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  Serial.printf("Progress: %u%%\r", (progress * 100) / total);
});

ArduinoOTA.onError([](ota_error_t error) {
  Serial.printf("Error[%u]\n", error);
});

ArduinoOTA.begin();
Serial.println("OTA Ready");


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
  finger.begin(FP_BAUD);

  if (!finger.verifyPassword()) {
    Serial.println("❌ Fingerprint sensor not detected");
    while (1);
  }

  BTSerial.begin(9600, SERIAL_8N1, BT_RX, BT_TX);
  BTSerial.println("Door Locked");

  if (EEPROM.read(OWNER_COUNT_ADDR) == 0) {
    Serial.println("🔑 No owners found – enroll first owner");
    enrollNewOwner();
  }

  Serial.println("🔐 Door Lock Ready - OTA Version 1");

  showPrompt();
}

// ================= LOOP =================
void loop() {

  ArduinoOTA.handle();
  yield();

  // ===== Process Pending ThingSpeak Update =====
if (pendingUpdate && millis() - lastTSUpdate >= 15000) {

  ThingSpeak.setField(1, pendingDoorStatus);
  ThingSpeak.setField(2, pendingAccessType);

  int response = ThingSpeak.writeFields(channelID, writeAPIKey);

  if (response == 200) {
    Serial.println("📡 Queued Data Sent to ThingSpeak");
    lastTSUpdate = millis();
    pendingUpdate = false;
  }
}

  
  if ((enrollMode || resetMode || normalEntryActive) &&
      millis() - modeTimer >= TIMEOUT_MS) {
    exitToMain();
    return;
  }

  // ===== BLUETOOTH =====
  if (!doorActive && BTSerial.available()) {
    String cmd = BTSerial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "OPEN") {
      if (cmd == "UPDATE") {
  Serial.println("📥 OTA Update Triggered");
  checkForOTAUpdate();
}


      Serial.println("📱 Bluetooth ACCESS GRANTED");
      BTSerial.println("Door Opened");

      digitalWrite(DOOR_LED, HIGH);
      doorActive = true;
      doorStartTime = millis();

      sendToThingSpeak(1, 3);
    }
  }

if (millis() - lastFingerCheck > fingerInterval) {
  lastFingerCheck = millis();
  checkFingerprint();
}
  // ===== AUTO CLOSE =====
  // ===== AUTO CLOSE AFTER 15 SECONDS =====
if (doorActive && millis() - doorStartTime >= doorDuration) {

  Serial.println("🔄 Door Closing...");
  BTSerial.println("Door Closing...");


  digitalWrite(DOOR_LED, LOW);
  doorActive = false;

  Serial.println("🔒 Door Locked");
  BTSerial.println("Door Locked");

  showPrompt();

  sendToThingSpeak(0, -1);   // Send CLOSE status
}

  char key = keypad.getKey();
  if (!key) return;

  modeTimer = millis();

  // ===== RESET MODE =====
if (resetMode) {

  if (key >= '0' && key <= '9') {
    input += key;
    Serial.print("*");
  }

  // Step 1: Check Admin Key
  if (!enteringNewPass && input.length() == 4) {
    Serial.println();

    if (input == adminKey) {
      input = "";
      enteringNewPass = true;
      Serial.println("Enter NEW 6-digit password:");
    } else {
      Serial.println("❌ Wrong admin key");
      exitToMain();
    }
  }

  // Step 2: Enter New Password
  else if (enteringNewPass && !confirmingPass && input.length() == 6) {
    newPass = input;
    input = "";
    confirmingPass = true;
    Serial.println("Confirm NEW password:");
  }

  // Step 3: Confirm Password
  else if (confirmingPass && input.length() == 6) {
    Serial.println();

    if (input == newPass) {
      password = newPass;
      savePassword(password);
      Serial.println("✅ Password changed SUCCESSFULLY");
    } else {
      Serial.println("❌ Password mismatch");
    }

    exitToMain();
  }

  return;
}


  if (key >= '0' && key <= '9') {
    normalEntryActive = true;
    input += key;
    Serial.print("*");
  }

  if (input.length() == 6) {
    Serial.println();
    if (input == password) {
      Serial.println("✅ ACCESS GRANTED");
      digitalWrite(DOOR_LED, HIGH);
      doorActive = true;
      doorStartTime = millis();

      sendToThingSpeak(1, 1);
    } else {
      Serial.println("❌ ACCESS DENIED");
    }
    input = "";
    normalEntryActive = false;
    showPrompt();
  }
}

// ================= FINGERPRINT =================
void checkFingerprint() {

  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz() != FINGERPRINT_OK) return;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return;

  Serial.println("🖐 Fingerprint ACCESS GRANTED");

  digitalWrite(DOOR_LED, HIGH);
  doorActive = true;
  doorStartTime = millis();

  sendToThingSpeak(1, 2);
}// ================= ENROLL OWNER =================
bool enrollNewOwner() {

  uint8_t newID = EEPROM.read(OWNER_COUNT_ADDR) + 1;

  Serial.print("👤 Enrolling OWNER ID ");
  Serial.println(newID);

  int p = -1;
  Serial.println("Place finger");

  while (p != FINGERPRINT_OK) {
  ArduinoOTA.handle();
  yield();
  p = finger.getImage();
  if (p == FINGERPRINT_NOFINGER) continue;
}


  while (finger.getImage() != FINGERPRINT_NOFINGER) {
  ArduinoOTA.handle();
  yield();
}


  Serial.println("Remove finger");
  delay(2000);

  while (finger.getImage() != FINGERPRINT_NOFINGER);

  Serial.println("Place SAME finger again");

  while (finger.getImage() != FINGERPRINT_OK) {
  ArduinoOTA.handle();
  yield();
}


  if (finger.image2Tz(2) != FINGERPRINT_OK) return false;
  if (finger.createModel() != FINGERPRINT_OK) return false;
  if (finger.storeModel(newID) != FINGERPRINT_OK) return false;

  EEPROM.write(OWNER_COUNT_ADDR, newID);
  EEPROM.commit();

  Serial.println("✅ New owner enrolled SUCCESSFULLY");
  return true;
}

// ================= HOLD EVENTS =================
void keypadEvent(KeypadEvent key) {

  if (keypad.getState() == HOLD && key == '#') {
    resetMode = true;
    input = "";
    modeTimer = millis();
    Serial.println("\n🔄 RESET PASSWORD MODE");
    Serial.println("Enter reset key:");
  }

  if (keypad.getState() == HOLD && (key == '1' || key == '2')) {
    enrollMode = true;
    adminInput = "";
    modeTimer = millis();
    Serial.println("\n🔐 ADD NEW OWNER MODE");
    Serial.println("Enter admin key:");
  }
}
