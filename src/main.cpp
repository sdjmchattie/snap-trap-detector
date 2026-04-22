#include <Arduino.h>

namespace {

constexpr uint8_t WAKE_PIN = 0;
constexpr uint8_t MIC_ADC_PIN = 3;
constexpr uint8_t BATTERY_ADC_PIN = 4;
constexpr uint8_t BUTTON_PIN = 5;
constexpr uint8_t LED_PIN = 8;

constexpr unsigned long LED_FLASH_MS = 500;
constexpr unsigned long POLL_DELAY_MS = 5;

bool wakeSignalLatched = false;
bool ledFlashActive = false;
unsigned long ledFlashStartedAtMs = 0;

void setLed(bool on) {
    digitalWrite(LED_PIN, on ? LOW : HIGH);
}

void configurePins() {
    pinMode(WAKE_PIN, INPUT_PULLUP);
    pinMode(MIC_ADC_PIN, INPUT);
    pinMode(BATTERY_ADC_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
}

void logPinConfiguration() {
    Serial.println("Pin configuration:");
    Serial.printf("  GPIO%d wake/comparator input (INPUT_PULLUP)\n", WAKE_PIN);
    Serial.printf("  GPIO%d microphone ADC input (INPUT)\n", MIC_ADC_PIN);
    Serial.printf("  GPIO%d battery ADC input (INPUT)\n", BATTERY_ADC_PIN);
    Serial.printf("  GPIO%d button input (INPUT_PULLUP)\n", BUTTON_PIN);
    Serial.printf("  GPIO%d onboard LED output (active low)\n", LED_PIN);
}

void handleWakeTrigger() {
    Serial.println("Wake trigger detected on GPIO0");
    ledFlashActive = true;
    ledFlashStartedAtMs = millis();
    setLed(true);
}

void updateWakeInput() {
    const bool wakeAsserted = digitalRead(WAKE_PIN) == LOW;

    if (wakeAsserted && !wakeSignalLatched) {
        wakeSignalLatched = true;
        handleWakeTrigger();
    } else if (!wakeAsserted && wakeSignalLatched) {
        wakeSignalLatched = false;
    }
}

void updateLedFlash() {
    if (!ledFlashActive) {
        return;
    }

    if (millis() - ledFlashStartedAtMs >= LED_FLASH_MS) {
        ledFlashActive = false;
        setLed(false);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(100);

    configurePins();

    Serial.println("Snap Trap Detector tuning firmware starting...");
    Serial.println("Always-awake mode enabled; deep sleep disabled for tuning.");
    logPinConfiguration();
}

void loop() {
    updateWakeInput();
    updateLedFlash();
    delay(POLL_DELAY_MS);
}
