# Claude Notes — snap-trap-detector

## Hardware

- **MCU**: ESP32-C3 Super Mini
  - Built-in LED on GPIO 8 (active low)
  - No ULP coprocessor — deep sleep wake must use GPIO or timer
- **Microphone**: MAX4466 microphone amplifier board
  - Outputs analog audio signal (centred ~VCC/2)
  - Gain adjustable via onboard trimmer
  - Output connected to an ESP32-C3 ADC pin

## Project logic

1. ESP32 sleeps in deep sleep
2. Woken by a loud sound (MAX4466 output exceeds threshold)
   - Requires external comparator circuit OR use of GPIO interrupt via envelope detector
3. On wake: monitor for a period of silence (~60 seconds)
   - If another loud event occurs within the window, reset the timer (could be a false trigger)
   - If silence holds for the full window, treat it as a confirmed trap event
4. Connect to WiFi, publish MQTT message, disconnect, return to deep sleep

## Wake-up strategy

The ESP32-C3 supports ext0/ext1 GPIO wakeup from deep sleep. The MAX4466 is analog, so to generate a digital wake signal one of:
- External comparator (e.g. LM393) with threshold set above ambient noise floor
- Envelope detector + comparator
- Resistor divider biasing into a Schmitt trigger

The wakeup GPIO must be an RTC-capable GPIO. On the C3, GPIOs 0–5 support deep-sleep wakeup.

## MQTT

- Topic TBD — likely something like `home/traps/<location>/triggered`
- Payload: JSON with timestamp and confidence/reason
- Broker credentials stored in a `secrets.h` (gitignored)

## Build

PlatformIO project, Arduino framework, ESP32-C3 target.
