#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NimBLEDevice.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define SLOT_FORK  16
#define SLOT_SPOON 17
#define SLOT_KNIFE 18

#define SERVICE_UUID  "12345678-1234-1234-1234-123456789abc"
#define UTENSIL_UUID  "12345678-1234-1234-1234-123456789abd"
#define STATUS_UUID   "12345678-1234-1234-1234-123456789abe"
#define SESSION_UUID  "12345678-1234-1234-1234-123456789abf"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

NimBLECharacteristic* utensilChar;
NimBLECharacteristic* statusChar;
NimBLECharacteristic* sessionChar;
NimBLEServer*         pServer;
NimBLEService*        pService;
NimBLEAdvertising*    pAdvertising;

String currentUtensil = "";
String lastUtensil    = "init";
String currentStatus  = "";
String lastStatus     = "init";
String pcStatus       = "ready";

bool feeding               = false;
unsigned long feedingStart = 0;
unsigned long lastSecond   = 0;

String getActiveUtensil() {
  bool forkSeated  = !digitalRead(SLOT_FORK);
  bool spoonSeated = !digitalRead(SLOT_SPOON);
  bool knifeSeated = !digitalRead(SLOT_KNIFE);

  int emptyCount = 0;
  if (!forkSeated)  emptyCount++;
  if (!spoonSeated) emptyCount++;
  if (!knifeSeated) emptyCount++;

  if (emptyCount == 0) return "none in use";
  if (emptyCount > 1)  return "error: check slots";
  if (!forkSeated)     return "Fork";
  if (!spoonSeated)    return "Spoon";
  if (!knifeSeated)    return "Knife";

  return "none in use";
}

String getStatus(String utensil) {
  if (utensil == "none in use")        return "idle";
  if (utensil == "error: check slots") return "error";
  if (pcStatus == "stopped")           return "stopped";
  if (pcStatus == "paused")            return "paused";
  if (pcStatus == "feeding")           return "feeding";
  return "ready";
}

String getSessionTime() {
  if (!feeding) return "--:--";
  unsigned long elapsed = (millis() - feedingStart) / 1000;
  unsigned long mins    = elapsed / 60;
  unsigned long secs    = elapsed % 60;
  String m = mins < 10 ? "0" + String(mins) : String(mins);
  String s = secs < 10 ? "0" + String(secs) : String(secs);
  return m + ":" + s;
}

void updateOLED(String utensil, String status, String sessionTime) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Feeder bot");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 16);
  display.print("In use:  ");
  display.println(utensil);

  display.setCursor(0, 28);
  display.print("Status:  ");
  display.println(status);

  display.setCursor(0, 40);
  display.print("Session: ");
  display.println(sessionTime);

  display.setCursor(0, 52);
  display.print("Slots: ");
  display.print(!digitalRead(SLOT_FORK)  ? "F " : "_ ");
  display.print(!digitalRead(SLOT_SPOON) ? "S " : "_ ");
  display.print(!digitalRead(SLOT_KNIFE) ? "K"  : "_");

  display.display();
}

void updateBLE(String utensil, String status, String sessionTime) {
  utensilChar->setValue(utensil.c_str());
  utensilChar->notify();
  statusChar->setValue(status.c_str());
  statusChar->notify();
  sessionChar->setValue(sessionTime.c_str());
  sessionChar->notify();
}

void checkSerial() {
  if (Serial.available()) {
    String received = Serial.readStringUntil('\n');
    received.trim();

    if (received == "feeding") {
      pcStatus = "feeding";
      if (!feeding) {
        feeding      = true;
        feedingStart = millis();
      }
    } else if (received == "ready") {
      pcStatus = "ready";
      feeding  = false;
    } else if (received == "stopped") {
      pcStatus = "stopped";
      feeding  = false;
    } else if (received == "paused") {
      pcStatus = "paused";
    }

    Serial.print("Received: ");
    Serial.println(received);
  }
}

class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pCharacteristic,
                   NimBLEConnInfo& connInfo,
                   uint16_t subValue) {
    Serial.print("Client subscribed to: ");
    Serial.println(pCharacteristic->getUUID().toString().c_str());
  }
};

static CharacteristicCallbacks chrCallbacks;

void setupBLE() {
  NimBLEDevice::init("Feeder Bot");
  pServer  = NimBLEDevice::createServer();
  pService = pServer->createService(SERVICE_UUID);

  utensilChar = pService->createCharacteristic(
    UTENSIL_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  utensilChar->setCallbacks(&chrCallbacks);

  statusChar = pService->createCharacteristic(
    STATUS_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  statusChar->setCallbacks(&chrCallbacks);

  sessionChar = pService->createCharacteristic(
    SESSION_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  sessionChar->setCallbacks(&chrCallbacks);

  pService->start();

  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE advertising as Feeder Bot");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);
  delay(2000);

  pinMode(SLOT_FORK,  INPUT_PULLUP);
  pinMode(SLOT_SPOON, INPUT_PULLUP);
  pinMode(SLOT_KNIFE, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED not found");
    while (true);
  }

  setupBLE();

  updateOLED("none in use", "idle", "--:--");
  Serial.println("Feeder bot ready");
}

void loop() {
  checkSerial();

  currentUtensil     = getActiveUtensil();
  currentStatus      = getStatus(currentUtensil);
  String sessionTime = getSessionTime();

  bool sessionTick = feeding && (millis() - lastSecond >= 1000);
  if (sessionTick) lastSecond = millis();

  if (currentUtensil != lastUtensil ||
      currentStatus  != lastStatus  ||
      sessionTick) {

    lastUtensil = currentUtensil;
    lastStatus  = currentStatus;

    Serial.print("Utensil: "); Serial.print(currentUtensil);
    Serial.print(" | Status: "); Serial.print(currentStatus);
    Serial.print(" | Session: "); Serial.println(sessionTime);

    updateOLED(currentUtensil, currentStatus, sessionTime);
    updateBLE(currentUtensil, currentStatus, sessionTime);
  }

  delay(100);
}
