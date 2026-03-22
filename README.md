# Snap Trap Detector

An ESP32-C3 Super Mini based device that listens for the sharp crack of a snap mousetrap and sends an MQTT notification — so you know to deal with the trap without having to check it manually.

## How it works

The device spends almost all of its time in deep sleep, drawing minimal current. A MAX4466 microphone amplifier board monitors ambient sound continuously. When a sudden loud sound occurs (such as a trap snapping), the ESP32-C3 wakes from deep sleep via a GPIO interrupt.

Once awake, the device listens for a further ~60 seconds. If the environment returns to silence — indicating a single sharp event rather than ongoing noise — it treats this as a likely trap trigger. It then:

1. Connects to WiFi
2. Publishes an MQTT message to a configured topic
3. Disconnects and returns to deep sleep

The silence-check step avoids false positives from sustained loud noise like music or conversation — a real trap snap is brief and isolated.

## Hardware

| Component | Notes |
|-----------|-------|
| ESP32-C3 Super Mini | Main MCU, WiFi, deep sleep |
| MAX4466 breakout | Microphone amplifier, analog audio output |
| Comparator circuit | Converts audio envelope to digital wake signal |
| 4× AA cells | Primary power supply (~6V) |
| MCP1700-3302E (LDO) | Regulates battery voltage to 3.3V for ESP32-C3 |

The MAX4466 output is analog. An external comparator (e.g. LM393) or envelope detector circuit converts loud sounds into a digital edge on an RTC-capable GPIO (GPIO 0–5 on the C3) to trigger deep-sleep wakeup.

## MQTT

On confirmed detection the device publishes to a configurable topic such as:

```
home/traps/kitchen/triggered
```

Payload is a JSON object including a timestamp and detection metadata.

## Configuration

Copy `include/secrets.h.example` to `include/secrets.h` and fill in:

```cpp
#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"
#define MQTT_HOST     "192.168.x.x"
#define MQTT_PORT     1883
#define MQTT_TOPIC         "home/traps/kitchen/triggered"
#define MQTT_BATTERY_TOPIC "home/traps/kitchen/battery"
```

## Build

Built with [PlatformIO](https://platformio.org/) using the Arduino framework.

```bash
pio run --target upload
```

## Power

The device runs from 4× AA cells regulated to 3.3V via an MCP1700-3302E LDO. With only ~1.6µA quiescent current, the MCP1700 adds negligible drain during deep sleep. Battery voltage is monitored via a resistor voltage divider into an ADC pin on the ESP32-C3, allowing the firmware to report battery status over MQTT.

Note: the MCP1700 has a maximum input voltage of 6V — fresh alkaline AA cells can measure up to ~6.4V, so either use NiMH cells (4× ~1.45V = 5.8V max) or include a small series diode to drop the voltage safely.

Deep sleep current on the ESP32-C3 is in the range of 5–20 µA, with the MAX4466 adding around 35 µA — giving a very long battery life between trap events.
