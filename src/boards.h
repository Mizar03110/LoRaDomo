// ================= boards.h — LoRaDomo V1.5 =================
//
// Board selection — three options in order of priority:
//
// 1. AUTO-DETECTION (recommended):
//    Select the correct board in Arduino IDE (Tools > Board) or in
//    platformio.ini (board = heltec_wifi_lora_32_V3).
//    LoRaDomo will detect the board automatically.
//
// 2. MANUAL OVERRIDE (if auto-detection fails):
//    Add ONE of these defines in your sketch BEFORE any #include:
//      #define LORADOMO_HELTEC_V2
//      #define LORADOMO_HELTEC_V3
//      #define LORADOMO_HELTEC_V4
//      #define LORADOMO_TTGO_V1
//
// 3. DEFAULT fallback: Heltec V3/V4 (SX1262) if nothing is detected.
//
#pragma once

// ── Step 1: auto-detect from Arduino/PlatformIO board selection ──────────────
#if !defined(LORADOMO_HELTEC_V2) && \
    !defined(LORADOMO_HELTEC_V3) && \
    !defined(LORADOMO_HELTEC_V4) && \
    !defined(LORADOMO_TTGO_V1)

  #if defined(ARDUINO_heltec_wifi_lora_32_V2) || \
      defined(ARDUINO_HELTEC_WIFI_LORA_32_V2) || \
      defined(CONFIG_IDF_TARGET_ESP32) && defined(HELTEC_LORA)
    #define LORADOMO_HELTEC_V2

  #elif defined(ARDUINO_heltec_wifi_lora_32_V3) || \
        defined(ARDUINO_HELTEC_WIFI_LORA_32_V3)
    #define LORADOMO_HELTEC_V3

  #elif defined(ARDUINO_heltec_wifi_lora_32_V4) || \
        defined(ARDUINO_HELTEC_WIFI_LORA_32_V4)
    #define LORADOMO_HELTEC_V4

  #elif defined(ARDUINO_TTGO_LoRa32_V1) || \
        defined(ARDUINO_TTGO_LORA32_V1)
    #define LORADOMO_TTGO_V1

  #endif
  // No match → default V3/V4 applied below
#endif

// ── Step 2: apply pin/driver definitions ─────────────────────────────────────

#if defined(LORADOMO_TTGO_V1)
// ── TTGO LoRa32 V1 — ESP32 + SX1276 (100% compatible Heltec V2) ──────────────
  #define LORADOMO_BOARD_NAME   "TTGO V1"
  #define LORADOMO_USE_SX1276

  #define LORA_CS    18
  #define LORA_RST   14
  #define LORA_DIO0  26
  #define LORA_FREQ  868.0f
  #define LORA_TX_DB 17
  #define LORA_BAT_PIN  13
  #define LORA_BAT_VREF 3.3f
  #define LORA_BAT_DIV  2.0f

#elif defined(LORADOMO_HELTEC_V2)
// ── Heltec V2 — ESP32 + SX1276 ───────────────────────────────────────────────
  #define LORADOMO_BOARD_NAME   "Heltec V2"
  #define LORADOMO_USE_SX1276

  #define LORA_CS    18
  #define LORA_RST   14
  #define LORA_DIO0  26
  #define LORA_FREQ  868.0f
  #define LORA_TX_DB 17
  #define LORA_BAT_PIN  13    // ADC pin for battery voltage (via voltage divider)
  #define LORA_BAT_VREF 3.3f  // ADC reference voltage
  #define LORA_BAT_DIV  2.0f  // voltage divider ratio

#elif defined(LORADOMO_HELTEC_V3)
// ── Heltec V3 — ESP32-S3 + SX1262 ────────────────────────────────────────────
  #define LORADOMO_BOARD_NAME   "Heltec V3"
  #define LORADOMO_USE_SX1262

  #define LORA_CS    8
  #define LORA_DIO1  14
  #define LORA_BUSY  13
  #define LORA_RST   12
  #define LORA_FREQ  868.0f
  #define LORA_TX_DB 13
  #define LORA_BAT_PIN      1     // ADC1_CH0 on ESP32-S3
  #define LORA_BAT_CTRL_PIN 37    // Must be LOW to enable battery ADC reading
  #define LORA_BAT_VREF     3.3f
  #define LORA_BAT_DIV      2.0f

#elif defined(LORADOMO_HELTEC_V4)
// ── Heltec V4 — ESP32-S3 + SX1262 ────────────────────────────────────────────
  #define LORADOMO_BOARD_NAME   "Heltec V4"
  #define LORADOMO_USE_SX1262

  #define LORA_CS    8
  #define LORA_DIO1  14
  #define LORA_BUSY  13
  #define LORA_RST   12
  #define LORA_FREQ  868.0f
  #define LORA_TX_DB 22    // V4 supports up to 27 dBm
  #define LORA_BAT_PIN      1
  #define LORA_BAT_CTRL_PIN 37    // Must be LOW to enable battery ADC reading
  #define LORA_BAT_VREF     3.3f
  #define LORA_BAT_DIV      2.0f

#else
// ── Default: V3/V4 ───────────────────────────────────────────────────────────
  #define LORADOMO_BOARD_NAME   "Heltec V3/V4 (default)"
  #define LORADOMO_USE_SX1262

  #define LORA_CS    8
  #define LORA_DIO1  14
  #define LORA_BUSY  13
  #define LORA_RST   12
  #define LORA_FREQ  868.0f
  #define LORA_TX_DB 13

#endif