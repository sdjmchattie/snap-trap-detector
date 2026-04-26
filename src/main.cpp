#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <stdarg.h>
#include "esp_sleep.h"
#include "secrets.h"

namespace {

// --- CONFIGURATION TOGGLE ---
constexpr bool SERIAL_DEBUG = false;
// ----------------------------

// Pin Definitions
constexpr uint8_t WAKE_PIN = 0;
constexpr uint8_t MIC_ADC_PIN = 3;
constexpr uint8_t BATTERY_ADC_PIN = 4;
constexpr uint8_t BUTTON_PIN = 5;
constexpr uint8_t LED_PIN = 8;

// Timing Constants
constexpr unsigned long SAMPLING_DURATION_MS = 10000;
constexpr unsigned long SAMPLE_INTERVAL_MS = 10; // 100 Hz
constexpr uint64_t SLEEP_DURATION_SEC = 24 * 60 * 60; // 24 hours

// Battery Constants (Voltage at GPIO pin)
constexpr float BATTERY_MAX_V = 2.85f;
constexpr float BATTERY_MIN_V = 1.85f;

// ADC Bias
constexpr int MIC_BIAS_MV = 1650;

// Globals
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

uint32_t micThreshold = 1000; // Default threshold for the sum
bool activationPossible = false;
bool thresholdUpdated = false;

// Centralized Logging Function
void log(const char* format, ...) {
    if (!SERIAL_DEBUG) return;
    va_list args;
    va_start(args, format);
    char buf[256];
    vsnprintf(buf, sizeof(buf), format, args);
    Serial.print(buf);
    va_end(args);
}

void setLed(bool on) {
    // LED_PIN 8 is active low on lolin_c3_mini
    digitalWrite(LED_PIN, on ? LOW : HIGH);
}

void configurePins() {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);

    pinMode(WAKE_PIN, INPUT_PULLUP);
    pinMode(MIC_ADC_PIN, INPUT);
    pinMode(BATTERY_ADC_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void connectToWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    log("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
        delay(500);
        log(".");
    }
    log(WiFi.status() == WL_CONNECTED ? " Connected!\n" : " Failed!\n");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    if (String(topic) == MQTT_TOPIC_CONFIG) {
        uint32_t newThreshold = message.toInt();
        if (newThreshold > 0) {
            micThreshold = newThreshold;
            preferences.putUInt("threshold", micThreshold);
            log("Updated threshold to: %u\n", micThreshold);
            thresholdUpdated = true;
        }
    }
}

void connectToMQTT() {
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    unsigned long startAttempt = millis();
    while (!mqttClient.connected() && millis() - startAttempt < 10000) {
        String clientId = "SnapTrap-" + String(random(0xffff), HEX);
        if (mqttClient.connect(clientId.c_str())) {
            log("MQTT Connected\n");
            mqttClient.subscribe(MQTT_TOPIC_CONFIG);
        } else {
            delay(1000);
            log("?");
        }
    }
}

float readBatteryVoltage() {
    // Read average of multiple samples for stability
    uint32_t sum = 0;
    for(int i=0; i<10; i++) {
        sum += analogReadMilliVolts(BATTERY_ADC_PIN);
        delay(5);
    }
    return (sum / 10.0f) / 1000.0f;
}

int calculateBatteryPercentage(float voltage) {
    if (voltage >= BATTERY_MAX_V) return 100;
    if (voltage <= BATTERY_MIN_V) return 0;
    return (int)((voltage - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V) * 100.0f);
}

void runMicValidation() {
    log("Starting microphone validation (10s)...\n");
    uint32_t totalAbsoluteDifference = 0;
    unsigned long startTime = millis();
    int sampleCount = 0;

    while (millis() - startTime < SAMPLING_DURATION_MS) {
        unsigned long nextSample = millis() + SAMPLE_INTERVAL_MS;

        int val = analogReadMilliVolts(MIC_ADC_PIN);
        totalAbsoluteDifference += abs(val - MIC_BIAS_MV);
        sampleCount++;

        long wait = nextSample - millis();
        if (wait > 0) delay(wait);
    }

    log("Validation complete. Samples: %d, Sum: %u, Threshold: %u\n",
                  sampleCount, totalAbsoluteDifference, micThreshold);

    // As per user requirement: "triggering ... if the sum is low"
    if (totalAbsoluteDifference < micThreshold) {
        activationPossible = true;
        log("Possible activation detected (Sum is LOW)\n");
    }
}

} // namespace

void setup() {
    if (SERIAL_DEBUG) {
        Serial.begin(115200);
        delay(1000);
    }

    configurePins();

    preferences.begin("snap-trap", false);
    micThreshold = preferences.getUInt("threshold", 1000);

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    log("\nWakeup cause: %d\n", wakeup_reason);

    // If woken by GPIO0
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        runMicValidation();
    }

    // Always report status and check for config
    connectToWiFi();
    if (WiFi.status() == WL_CONNECTED) {
        connectToMQTT();
        if (mqttClient.connected()) {
            // Report Battery
            float battV = readBatteryVoltage();
            int battPct = calculateBatteryPercentage(battV);
            String statusMsg = "{\"battery_v\":" + String(battV, 2) +
                               ",\"battery_pct\":" + String(battPct) + "}";
            mqttClient.publish(MQTT_TOPIC_STATUS, statusMsg.c_str());
            log("Published status: %s\n", statusMsg.c_str());

            // Report Activation if triggered
            if (activationPossible) {
                mqttClient.publish(MQTT_TOPIC_ACTIVATION, "Possible activation detected");
                log("Published activation alert\n");
            }

            // Wait for threshold update if any
            unsigned long listenStart = millis();
            while (millis() - listenStart < 2000) {
                mqttClient.loop();
                if (thresholdUpdated) break;
                delay(10);
            }
        }
    }

    log("Entering deep sleep...\n");
    if (SERIAL_DEBUG) Serial.flush();

    // Wake on GPIO0 LOW or 24 hour timer
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_SEC * 1000000ULL);

    preferences.end();
    esp_deep_sleep_start();
}

void loop() {
    // Never reached
}
