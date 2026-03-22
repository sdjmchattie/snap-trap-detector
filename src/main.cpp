#include <Arduino.h>

#define LED_PIN 8

void setup() {
    Serial.begin(115200);
    Serial.println("Snap Trap Detector starting...");
    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_PIN, LOW);   // LED on (active low)
    delay(500);
    digitalWrite(LED_PIN, HIGH);  // LED off
    delay(500);
}
