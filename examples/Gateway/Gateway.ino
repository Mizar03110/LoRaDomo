// ================= example_gateway.ino — LoRaDomo V1.1 =================
// Example gateway sketch for Heltec V3

// Define your board BEFORE including the library:
//#define LORADOMO_HELTEC_V2
//#define LORADOMO_HELTEC_V3   // or LORADOMO_HELTEC_V4
//#define LORADOMO_TTGO_V1

#include "LoRaGateway.h"

LoRaGateway gateway;

void setup() {
    Serial.begin(115200);

    gateway.begin(
        "TestGTW1",         // gateway name (MQTT prefix + web UI)
        "1234",             // shared network key
        "192.168.1.8",      // MQTT broker
        "TestGTW",          // MQTT username
        "Miaouche4059!",    // MQTT password
        "IOT",              // WiFi SSID
        "wufuwufu",          // WiFi password
        true                // debug mode (prints more info to Serial)
    );
}
 
void loop() {
    gateway.loop();
}

// MQTT topics published:
//   TestGTW1/OUT/TestGTW1/status            -> "1" online / "0" offline (LWT)
//   TestGTW1/OUT/<nodeName>/status      -> "online" / "offline"
//   TestGTW1/OUT/<nodeName>/<sensorName> -> numeric value
//
// Web UI : http://<gateway_ip>/
// WebSocket : ws://<gateway_ip>:81
