#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#define SW_WATER 5
#define DHT_PIN 4
#define DHT_TYPE DHT22

#define SERVICE_UUID \
  "7d3a1000-4c1e-4d7f-9a01-000000000001"

#define CONTROL_UUID \
  "7d3a1000-4c1e-4d7f-9a01-000000000002"

#define PROGRAM_UUID \
  "7d3a1000-4c1e-4d7f-9a01-000000000003"

#define STATUS_UUID \
  "7d3a1000-4c1e-4d7f-9a01-000000000004"

const char* DEVICE_NAME =
  "ESP32_Smart_Seedling_Waterer";

DHT dht(DHT_PIN, DHT_TYPE);
Preferences storage;

BLEServer* bleServer = nullptr;
BLECharacteristic* controlCharacteristic = nullptr;
BLECharacteristic* programCharacteristic = nullptr;
BLECharacteristic* statusCharacteristic = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;

bool waterState = false;
bool programRunning = false;

String programJson =
  "{\"name\":\"Empty Program\",\"steps\":[]}";

String incomingProgram = "";

size_t currentStep = 0;

bool waiting = false;
unsigned long waitUntil = 0;

unsigned long lastRuntimeStep = 0;
unsigned long lastStatusUpdate = 0;

unsigned long savedUnixTime = 0;
unsigned long savedMillis = 0;


class ServerCallbacks : public BLEServerCallbacks {

  void onConnect(BLEServer* server) override {
    deviceConnected = true;

    Serial.println(
      "BLE client connected"
    );
  }

  void onDisconnect(BLEServer* server) override {
    deviceConnected = false;

    Serial.println(
      "BLE client disconnected"
    );

    BLEDevice::startAdvertising();
  }
};


void setWater(bool state) {
  waterState = state;

  digitalWrite(
    SW_WATER,
    state ? HIGH : LOW
  );
}


float readHumidity() {
  float value =
    dht.readHumidity();

  if (isnan(value)) {
    return NAN;
  }

  return value;
}


float readTemperature() {
  float value =
    dht.readTemperature();

  if (isnan(value)) {
    return NAN;
  }

  return value;
}


void loadProgram() {
  storage.begin(
    "visualprog",
    true
  );

  programJson =
    storage.getString(
      "program",
      "{\"name\":\"Empty Program\",\"steps\":[]}"
    );

  savedUnixTime =
    storage.getULong(
      "time",
      0
    );

  storage.end();

  if (programJson.length() == 0) {
    programJson =
      "{\"name\":\"Empty Program\",\"steps\":[]}";
  }
}


void saveProgram() {
  storage.begin(
    "visualprog",
    false
  );

  storage.putString(
    "program",
    programJson
  );

  storage.end();

  Serial.println(
    "Program saved to NVS"
  );
}


void saveTime() {
  storage.begin(
    "visualprog",
    false
  );

  storage.putULong(
    "time",
    savedUnixTime
  );

  storage.end();
}


unsigned long currentUnixTime() {
  if (savedUnixTime == 0) {
    return 0;
  }

  return savedUnixTime +
    (
      (
        millis() - savedMillis
      ) / 1000
    );
}


void sendControlMessage(
  const String& message
) {
  Serial.print(
    "Response: "
  );

  Serial.println(
    message
  );

  controlCharacteristic->setValue(
    message
  );

  if (deviceConnected) {
    controlCharacteristic->notify();
  }
}


void sendProgram() {
  controlCharacteristic->setValue(
    programJson
  );

  if (deviceConnected) {
    controlCharacteristic->notify();
  }
}


void sendStatus() {
  float humidity =
    readHumidity();

  float temperature =
    readTemperature();

  String json;

  json.reserve(256);

  json += "{";

  json += "\"device\":\"";
  json += DEVICE_NAME;
  json += "\"";

  json += ",\"water\":";
  json += waterState
    ? "true"
    : "false";

  json += ",\"running\":";
  json += programRunning
    ? "true"
    : "false";

  json += ",\"humidity\":";

  if (isnan(humidity)) {
    json += "null";
  } else {
    json += String(
      humidity,
      1
    );
  }

  json += ",\"temperature\":";

  if (isnan(temperature)) {
    json += "null";
  } else {
    json += String(
      temperature,
      1
    );
  }

  json += ",\"time\":";
  json += String(
    currentUnixTime()
  );

  json += "}";

  statusCharacteristic->setValue(
    json
  );

  if (deviceConnected) {
    statusCharacteristic->notify();
  }
}


bool validateProgram(
  const String& candidate
) {
  ArduinoJson::JsonDocument doc;

  ArduinoJson::DeserializationError error =
    ArduinoJson::deserializeJson(
      doc,
      candidate
    );

  if (error) {
    Serial.print(
      "JSON error: "
    );

    Serial.println(
      error.c_str()
    );

    return false;
  }

  if (
    !doc["steps"].is<ArduinoJson::JsonArray>()
  ) {
    Serial.println(
      "Program has no steps array"
    );

    return false;
  }

  return true;
}


void displayProgramValue(
  ArduinoJson::JsonObject step
) {
  const char* label =
    step["label"] | "";

  ArduinoJson::JsonVariant value =
    step["value"];

  String outputValue = "";


  if (
    value.is<ArduinoJson::JsonObject>()
  ) {
    ArduinoJson::JsonObject valueObject =
      value.as<ArduinoJson::JsonObject>();

    const char* source =
      valueObject["source"] | "";


    if (
      strcmp(
        source,
        "temperature"
      ) == 0
    ) {
      float temperature =
        readTemperature();

      if (isnan(temperature)) {
        outputValue = "--";
      } else {
        outputValue =
          String(
            temperature,
            1
          );

        outputValue +=
          " °C";
      }
    }


    else if (
      strcmp(
        source,
        "humidity"
      ) == 0
    ) {
      float humidity =
        readHumidity();

      if (isnan(humidity)) {
        outputValue = "--";
      } else {
        outputValue =
          String(
            humidity,
            1
          );

        outputValue +=
          " %";
      }
    }


    else if (
      strcmp(
        source,
        "water"
      ) == 0
    ) {
      outputValue =
        waterState
          ? "ON"
          : "OFF";
    }


    else if (
      strcmp(
        source,
        "text"
      ) == 0
    ) {
      const char* text =
        valueObject["text"] | "";

      outputValue =
        text;
    }
  }


  String response;

  response.reserve(256);

  response +=
    "{\"event\":\"display\",\"label\":\"";

  response +=
    label;

  response +=
    "\",\"value\":\"";

  response +=
    outputValue;

  response +=
    "\"}";


  Serial.println(
    response
  );

  controlCharacteristic->setValue(
    response
  );

  if (deviceConnected) {
    controlCharacteristic->notify();
  }
}


void executeStep(
  ArduinoJson::JsonObject step
) {
  const char* type =
    step["type"] | "";


  if (
    strcmp(
      type,
      "start"
    ) == 0
  ) {
    return;
  }


  if (
    strcmp(
      type,
      "water"
    ) == 0
  ) {
    bool state =
      step["state"] | false;

    setWater(
      state
    );

    return;
  }


  if (
    strcmp(
      type,
      "wait"
    ) == 0
  ) {
    unsigned long duration =
      step["ms"] | 0;

    if (duration > 0) {
      waitUntil =
        millis() + duration;

      waiting = true;
    }

    return;
  }


  if (
    strcmp(
      type,
      "display"
    ) == 0
  ) {
    displayProgramValue(
      step
    );

    return;
  }


  if (
    strcmp(
      type,
      "sensor"
    ) == 0
  ) {
    return;
  }
}


void runProgram() {
  if (!programRunning) {
    return;
  }


  if (waiting) {
    if (
      millis() <
      waitUntil
    ) {
      return;
    }

    waiting = false;
  }


  ArduinoJson::JsonDocument doc;

  ArduinoJson::DeserializationError error =
    ArduinoJson::deserializeJson(
      doc,
      programJson
    );

  if (error) {
    programRunning = false;

    setWater(false);

    sendControlMessage(
      "PROGRAM_PARSE_ERROR"
    );

    return;
  }


  ArduinoJson::JsonArray steps =
    doc["steps"].as<ArduinoJson::JsonArray>();


  if (
    currentStep >= steps.size()
  ) {
    programRunning = false;

    setWater(false);

    sendControlMessage(
      "PROGRAM_FINISHED"
    );

    return;
  }


  ArduinoJson::JsonObject step =
    steps[currentStep]
      .as<ArduinoJson::JsonObject>();


  executeStep(
    step
  );


  if (!waiting) {
    currentStep++;
  }
}


void handleControlCommand(
  String command
) {
  command.trim();

  Serial.print(
    "BLE command: "
  );

  Serial.println(
    command
  );


  if (
    command == "PING"
  ) {
    sendControlMessage(
      "PONG"
    );

    return;
  }


  if (
    command == "STATUS"
  ) {
    sendStatus();

    return;
  }


  if (
    command == "GET_PROGRAM"
  ) {
    sendProgram();

    return;
  }


  if (
    command == "SAVE_PROGRAM"
  ) {
    saveProgram();

    sendControlMessage(
      "PROGRAM_SAVED"
    );

    return;
  }


  if (
    command == "RUN"
  ) {
    currentStep = 0;

    waiting = false;

    waitUntil = 0;

    programRunning = true;

    sendControlMessage(
      "PROGRAM_STARTED"
    );

    return;
  }


  if (
    command == "STOP"
  ) {
    programRunning = false;

    waiting = false;

    waitUntil = 0;

    setWater(false);

    sendControlMessage(
      "PROGRAM_STOPPED"
    );

    return;
  }


  if (
    command == "WATER_ON"
  ) {
    setWater(true);

    sendControlMessage(
      "WATER_ON"
    );

    return;
  }


  if (
    command == "WATER_OFF"
  ) {
    setWater(false);

    sendControlMessage(
      "WATER_OFF"
    );

    return;
  }


  if (
    command.startsWith(
      "TIME "
    )
  ) {
    String value =
      command.substring(5);

    unsigned long unixTime =
      value.toInt();

    if (
      unixTime > 0
    ) {
      savedUnixTime =
        unixTime;

      savedMillis =
        millis();

      saveTime();

      sendControlMessage(
        "TIME_SET"
      );
    }

    return;
  }


  if (
    command == "PROGRAM_BEGIN"
  ) {
    incomingProgram = "";

    sendControlMessage(
      "PROGRAM_BEGIN_OK"
    );

    return;
  }


  if (
    command == "PROGRAM_END"
  ) {
    if (
      incomingProgram.length() == 0
    ) {
      sendControlMessage(
        "PROGRAM_EMPTY"
      );

      return;
    }


    if (
      !validateProgram(
        incomingProgram
      )
    ) {
      sendControlMessage(
        "PROGRAM_INVALID"
      );

      return;
    }


    programJson =
      incomingProgram;

    saveProgram();

    sendControlMessage(
      "PROGRAM_OK"
    );

    return;
  }


  sendControlMessage(
    "UNKNOWN_COMMAND"
  );
}


class ControlCallbacks
  : public BLECharacteristicCallbacks {

  void onWrite(
    BLECharacteristic* characteristic
  ) override {

    String value =
      characteristic->getValue();

    if (
      value.length() == 0
    ) {
      return;
    }

    handleControlCommand(
      value
    );
  }
};


class ProgramCallbacks
  : public BLECharacteristicCallbacks {

  void onWrite(
    BLECharacteristic* characteristic
  ) override {

    String chunk =
      characteristic->getValue();

    if (
      chunk.length() == 0
    ) {
      return;
    }

    incomingProgram +=
      chunk;

    Serial.print(
      "Program chunk received: "
    );

    Serial.print(
      chunk.length()
    );

    Serial.print(
      " bytes, total = "
    );

    Serial.println(
      incomingProgram.length()
    );
  }
};


void setupBLE() {

  BLEDevice::init(
    DEVICE_NAME
  );


  bleServer =
    BLEDevice::createServer();


  bleServer->setCallbacks(
    new ServerCallbacks()
  );


  BLEService* service =
    bleServer->createService(
      SERVICE_UUID
    );


  controlCharacteristic =
    service->createCharacteristic(
      CONTROL_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY
    );


  programCharacteristic =
    service->createCharacteristic(
      PROGRAM_UUID,
      BLECharacteristic::PROPERTY_WRITE
    );


  statusCharacteristic =
    service->createCharacteristic(
      STATUS_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_NOTIFY
    );


  controlCharacteristic->addDescriptor(
    new BLE2902()
  );


  statusCharacteristic->addDescriptor(
    new BLE2902()
  );


  controlCharacteristic->setCallbacks(
    new ControlCallbacks()
  );


  programCharacteristic->setCallbacks(
    new ProgramCallbacks()
  );


  controlCharacteristic->setValue(
    "READY"
  );


  statusCharacteristic->setValue(
    "{\"device\":\"ESP32_Smart_Seedling_Waterer\",\"water\":false,\"running\":false}"
  );


  service->start();


  BLEAdvertising* advertising =
    BLEDevice::getAdvertising();


  advertising->addServiceUUID(
    SERVICE_UUID
  );


  advertising->setScanResponse(
    true
  );


  BLEDevice::startAdvertising();


  Serial.println(
    "BLE advertising started"
  );

  Serial.print(
    "Device name: "
  );

  Serial.println(
    DEVICE_NAME
  );
}


void setup() {

  Serial.begin(
    115200
  );


  pinMode(
    SW_WATER,
    OUTPUT
  );


  setWater(
    false
  );


  dht.begin();


  loadProgram();


  savedMillis =
    millis();


  Serial.println();

  Serial.println(
    "=========================================="
  );

  Serial.println(
    "ESP32_Smart_Seedling_Waterer"
  );

  Serial.println(
    "Bluetooth Visual Automation Runtime"
  );

  Serial.println(
    "=========================================="
  );


  setupBLE();


  Serial.println(
    "Runtime ready"
  );
}


void loop() {

  if (
    millis() -
    lastRuntimeStep >=
    50
  ) {

    lastRuntimeStep =
      millis();

    runProgram();
  }


  if (
    millis() -
    lastStatusUpdate >=
    1000
  ) {

    lastStatusUpdate =
      millis();

    if (
      deviceConnected
    ) {
      sendStatus();
    }
  }


  if (
    !deviceConnected &&
    oldDeviceConnected
  ) {

    delay(
      500
    );

    oldDeviceConnected =
      deviceConnected;
  }


  if (
    deviceConnected &&
    !oldDeviceConnected
  ) {

    oldDeviceConnected =
      deviceConnected;
  }
}