// ================= example_node.ino — LoRaDomo V1.0.0 =================
// Example node sketch for Heltec V3

#include "LoRaNode.h"
#include <DHT.h>   // example sensor library

LoRaNode node;

#define SENSOR_TEMP    1   // TYPE_FLOAT — sent every 30s, read via callback
#define SENSOR_HUM     2   // TYPE_INT8  — sent every 60s, read via callback
#define SENSOR_RELAY   3   // TYPE_INT8  — sent on change only, actuator callback

DHT dht(4, DHT22);

// --- Read callbacks: called just before each automatic send ---
float readTemperature() { return dht.readTemperature(); }
int8_t readHumidity()   { return (int8_t)dht.readHumidity(); }

// --- Actuator callback: called when gateway sends a value ---
void onRelayCommand(uint8_t sensorID, int8_t value) {
    Serial.printf("[App] Relay command: sensorID=%d value=%d\n", sensorID, value);
    // digitalWrite(RELAY_PIN, value ? HIGH : LOW);
    node.sendInt8(sensorID, value);   // confirm new state to gateway
}

void setup() {
    Serial.begin(115200);
    dht.begin();

    node.begin("myLoRaNetwork", "living_room", true);  // true = enable debug output

    // addSensor(id, type, name, sendInterval, actCallback, readCallback)
    node.addSensor(SENSOR_TEMP,  TYPE_FLOAT, "temperature", 30,
                   nullptr, (void*)readTemperature);

    node.addSensor(SENSOR_HUM,   TYPE_INT8,  "humidity",    60,
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
