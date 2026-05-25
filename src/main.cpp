#include <Arduino.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include "driver/twai.h"

// --- Heltec WiFi LoRa 32 V3 OLED pins ---
#define OLED_SDA  17
#define OLED_SCL  18
#define OLED_RST  21
#define VEXT_PIN  36

// --- CAN / TWAI pins (TJA1050) ---
#define CAN_TX    48
#define CAN_RX    47

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL, GEOMETRY_128_64);

static uint32_t rxCount = 0;
static uint32_t errCount = 0;

// Ring buffer to show last 3 received packets on OLED
struct RxEntry {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
};
static RxEntry history[3];
static int histIdx = 0;

void resetOLED() {
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(20);
    digitalWrite(OLED_RST, HIGH);
    delay(20);
}

void drawScreen(const char *status) {
    display.clear();

    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "CAN RX Demo");

    display.setFont(ArialMT_Plain_10);

    char buf[48];
    snprintf(buf, sizeof(buf), "RX: %lu  Err: %lu", (unsigned long)rxCount, (unsigned long)errCount);
    display.drawString(0, 17, buf);

    // Show last 3 received packets
    for (int i = 0; i < 3; i++) {
        int idx = (histIdx - 1 - i + 3) % 3;
        RxEntry &e = history[idx];
        if (e.dlc == 0 && e.id == 0) continue;

        char line[40];
        int pos = snprintf(line, sizeof(line), "%03lX: ", (unsigned long)e.id);
        for (int b = 0; b < e.dlc && pos < (int)sizeof(line) - 4; b++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", e.data[b]);
        }
        display.drawString(0, 29 + i * 12, line);
    }

    display.display();
}

void initCAN() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        Serial.printf("[CAN] Driver install FAILED: 0x%X\n", err);
        display.clear();
        display.setFont(ArialMT_Plain_16);
        display.drawString(0, 20, "CAN FAIL!");
        display.display();
        while (true) delay(1000);
    }

    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("[CAN] Start FAILED: 0x%X\n", err);
        display.clear();
        display.setFont(ArialMT_Plain_16);
        display.drawString(0, 20, "CAN Start FAIL!");
        display.display();
        while (true) delay(1000);
    }

    Serial.println("[CAN] Started @ 500 kbps — listening");
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== ESP32 LoRa V3 - CAN RX Demo ===");
    Serial.printf("CAN TX=GPIO%d  RX=GPIO%d  TJA1050\n", CAN_TX, CAN_RX);

    // Power on OLED
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);
    delay(50);

    resetOLED();
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 20, "CAN Init...");
    display.display();

    initCAN();

    memset(history, 0, sizeof(history));
    drawScreen("Waiting...");
}

void loop() {
    twai_message_t msg;
    esp_err_t result = twai_receive(&msg, pdMS_TO_TICKS(100));

    if (result == ESP_OK) {
        if (msg.rtr) return;

        rxCount++;

        // Store in ring buffer
        history[histIdx].id = msg.identifier;
        history[histIdx].dlc = msg.data_length_code;
        memcpy(history[histIdx].data, msg.data, 8);
        histIdx = (histIdx + 1) % 3;

        // Serial log
        Serial.printf("[RX #%lu] ID=0x%03lX [%d] ",
                      (unsigned long)rxCount,
                      (unsigned long)msg.identifier,
                      msg.data_length_code);
        for (int i = 0; i < msg.data_length_code; i++) {
            Serial.printf("%02X ", msg.data[i]);
        }
        Serial.println();

        drawScreen("Receiving");

    } else if (result != ESP_ERR_TIMEOUT) {
        errCount++;
        Serial.printf("[CAN] Receive error: 0x%X\n", result);
        drawScreen("Error!");
    }
}
