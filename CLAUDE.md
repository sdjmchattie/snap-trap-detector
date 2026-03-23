# Claude Notes — snap-trap-detector (Rodent Responder)

Battery-powered ESP32-C3 SuperMini that listens for mouse trap snaps via a microphone and comparator circuit, wakes from deep sleep, evaluates whether the sound was a genuine trap event (vs kitchen noise), and sends an MQTT notification. Designed to be mounted under kitchen cupboards near snap traps.

## Hardware

### MCU

- **ESP32-C3 SuperMini** (ESP32-C3FN4 chip)
- Built-in LED on GPIO 8 (active low)
- No ULP coprocessor — deep sleep wake must use GPIO or timer
- GPIOs 0–5 support deep-sleep wakeup (RTC-capable)

### Pin Assignments

| Pin         | Function                                                          |
| ----------- | ----------------------------------------------------------------- |
| 3V3         | Power input from LDO regulator                                    |
| GND         | Ground rail                                                       |
| GPIO2       | Wake pin — comparator output (wake on LOW)                        |
| GPIO3 (A3)  | ADC — raw mic output (software noise analysis after wake)         |
| GPIO8       | Built-in LED (status indication)                                  |
| GPIOx (TBD) | ADC — battery voltage divider output                              |
| GPIOx (TBD) | Momentary push button (internal pull-up, monitoring mode at boot) |

### Power Supply

- 4× AA batteries (alkaline or NiMH) in a 2×2 holder (~4.8–6.4V)
- 1N4007 diode in series on positive line (reverse polarity protection, ~0.7V drop)
- 500mA SMD fuse (1206) in series on positive battery line
- MCP1700-3302E LDO regulator → 3.3V rail (~1.6µA quiescent current)
- 3.6V zener diode (reverse-biased) on regulator output (clamp against regulator failure)
- C1: 100nF ceramic capacitor on regulator input
- C5: 220µF electrolytic capacitor on regulator output (bulk cap for WiFi TX bursts)
- Battery voltage after the reverse polarity diode fed through 1MΩ/1MΩ divider to ADC pin (half actual voltage)

### Microphone

- MAX4466 electret mic module, gain adjustable 25×–125× via onboard trimpot
- Powered from 3.3V rail; output centred on ~1.65V (VCC/2)
- Mic output connected to two places:
  1. Through C3 (10µF electrolytic coupling cap) → LM393 comparator non-inverting input
  2. Directly to GPIO3 ADC for software noise analysis

### Comparator (Hardware Wake Trigger)

- LM393 dual comparator (one channel used; unused inputs tied to GND, unused output floating)
- Non-inverting input (+, pin 3): AC-coupled mic signal, DC-biased to 1.65V via R1/R2 (100kΩ/100kΩ from 3.3V)
- Inverting input (−, pin 2): Fixed threshold ~1.96V via R9/R10 (68kΩ/100kΩ from 3.3V)
- Output (pin 1): Open-collector, pulled HIGH by R3 (10kΩ to 3.3V). Goes LOW when sound exceeds threshold.
- Output → GPIO2 (ESP32 wakes on LOW)

## Firmware Behaviour

### Boot Mode Selection

- **Production mode** (default): button NOT pressed at boot
- **Monitoring mode**: button HELD at boot — flash LED for 10 seconds to confirm, then stay awake

### Production Mode

1. Deep sleep with two simultaneous wake sources:
   - GPIO2 LOW (comparator trigger)
   - RTC timer (configurable heartbeat interval, default 24 hours)

2. On wake, check `esp_sleep_get_wakeup_cause()`:

**GPIO wake (sound detected):**

- Sample ADC on GPIO3 at ~100Hz for `listen_duration_ms` (default 5000ms)
- Sum absolute deviations from 1.65V baseline (~2048 on 12-bit ADC)
- If total noise BELOW `noise_threshold` → isolated snap → trap event:
  - Connect to WiFi (use BSSID/channel cached in RTC memory for fast reconnect)
  - Publish trap triggered MQTT message (include battery %)
  - Check retained MQTT config message (threshold updates or OTA URL)
  - Disconnect WiFi
- If total noise ABOVE `noise_threshold` → ongoing noise → false trigger:
  - Return to deep sleep without WiFi

**Timer wake (daily heartbeat):**

- Attach falling-edge interrupt on GPIO2 that sets a `snap_detected` flag
- Connect to WiFi
- Publish heartbeat MQTT message (include battery %)
- Check retained MQTT config message
- If OTA URL found: download and flash firmware
- If threshold config found: update NVS, clear retained MQTT message
- Disconnect WiFi
- Detach GPIO2 interrupt
- If `snap_detected` flag is set: run full trap-detection flow (ADC noise sampling → MQTT alert if confirmed)
- Return to deep sleep

### Monitoring Mode

- Stay awake continuously (no deep sleep)
- Sample GPIO3 ADC at 100Hz via hardware timer ISR
- Buffer samples in RAM (~350KB ≈ 58,000 samples ≈ ~10 minutes)
- When buffer full:
  - Connect to WiFi
  - Sync clock via NTP (non-blocking, runs during upload)
  - POST data batch to HTTP endpoint on local Pi server (with timestamp of first sample)
  - Clear buffer, disconnect WiFi, resume sampling (ISR continues during upload)
- No MQTT in this mode — raw audio data only

## MQTT Topics

- `rodent-responder/{device_id}/event` — trap triggered alert
- `rodent-responder/{device_id}/heartbeat` — daily status with battery %
- `rodent-responder/{device_id}/config` — retained message for config updates (device reads and clears)

Broker credentials stored in `secrets.h` (gitignored).

## Configurable Parameters (NVS, updatable via MQTT config topic)

| Key                    | Default | Description                                               |
| ---------------------- | ------- | --------------------------------------------------------- |
| `noise_threshold`      | TBD     | Max acceptable total noise during post-wake listen window |
| `listen_duration_ms`   | 5000    | How long to sample after GPIO wake                        |
| `heartbeat_interval_s` | 86400   | Seconds between RTC timer heartbeat wakes                 |
| `ota_url`              | —       | URL for firmware binary; cleared after successful flash   |

## Battery Voltage

- ADC reads 1MΩ/1MΩ divider (= half battery voltage after diode)
  - 1.85v is 0% remaining
  - 2.85v is 100% remaining

## Build

- **Framework**: Arduino via PlatformIO
- **Board**: `esp32-c3-devkitm-1` (or SuperMini equivalent)
- **Build environments**: `production`, `monitor`
- **Build flags**: `-DPRODUCTION`, `-DMONITOR_MODE`
- **Debug logging**: `emitSerialLog()` wrapper — all debug output behind `#ifdef DEBUG_MODE`, excluded from production builds; WiFi serial logging available in debug builds
