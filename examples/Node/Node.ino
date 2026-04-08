// ================= example_node.ino — LoRaDomo V1.1 =================
// Example node sketch for Heltec V3


// Define your board BEFORE including the library
// or comment all these lines to use board choosen in IDE
//#define LORADOMO_HELTEC_V2
//#define LORADOMO_HELTEC_V3   // or LORADOMO_HELTEC_V4
//#define LORADOMO_TTGO_V1

#include "LoRaNode.h"

LoRaNode node;

#define SENSOR_TEMP    1   // TYPE_FLOAT — sent every 30s, read via callback
#define SENSOR_HUM     2   // TYPE_INT8  — sent every 60s, read via callback
#define SENSOR_RELAY   3   // TYPE_INT8  — sent on change only, actuator callback


// --- Read callbacks: called just before each automatic send ---
float readTemperature() { return  21.5f + (random(-5, 6) / 10.0f); }
int8_t readHumidity()   { return  58 + random(-2, 3); }

// --- Actuator callback: called when gateway sends a value ---
void onRelayCommand(uint8_t sensorID, int8_t value) {
    Serial.printf("[App] Relay command: sensorID=%d value=%d\n", sensorID, value);
    // digitalWrite(RELAY_PIN, value ? HIGH : LOW);
    node.sendInt8(sensorID, value);   // confirm new state to gateway
}

void myBattery(bool& isUSB, uint8_t& battery) {
#ifdef LORA_BAT_CTRL_PIN
    pinMode(LORA_BAT_CTRL_PIN, OUTPUT);
    digitalWrite(LORA_BAT_CTRL_PIN, LOW); delay(5);
#endif
    float adc = (analogRead(LORA_BAT_PIN) / 4095.0f) * LORA_BAT_VREF;
    float v   = adc * LORA_BAT_DIV;
#ifdef LORA_BAT_CTRL_PIN
    digitalWrite(LORA_BAT_CTRL_PIN, HIGH);
#endif
    isUSB = (v > 4.3f);
    if (isUSB) { battery = 100; return; }
    float pct = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0.0f) pct = 0.0f; if (pct > 100.0f) pct = 100.0f;
    battery = (uint8_t)pct;
}

void setup() {
    Serial.begin(115200);

    node.begin("1234", "TestNode1", true);
    //node.setBatteryCallback(myBattery); //USB by default, uncomment to use custom battery reading function

    // addSensor(id, type, name, sendInterval, actCallback, readCallback)
    node.addSensor(SENSOR_TEMP,  TYPE_FLOAT, "temperature", 3,
                   nullptr, (void*)readTemperature);

    node.addSensor(SENSOR_HUM,   TYPE_INT8,  "humidity",    3,
                   nullptr, (void*)readHumidity);

    node.addSensor(SENSOR_RELAY, TYPE_INT8,  "relay",        0,
                   (void*)onRelayCommand, nullptr);

    // Pre-load initial values (e.g. from EEPROM) — sent on first REGISTERED burst
    node.setSensorFloat(SENSOR_TEMP,  21.5f);
    node.setSensorInt8 (SENSOR_HUM,   58);
    node.setSensorInt8 (SENSOR_RELAY, 0);
}

void loop() {
    node.loop();   // handles registration, auto-send, heartbeat, actuators
}
