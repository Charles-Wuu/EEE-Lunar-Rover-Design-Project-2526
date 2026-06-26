# Firmware

Arduino sketches for the rover control, detection, and RF receiver work.

## Final sketch

`arduino/final/control_monitor_combined/control_monitor_combined.ino` is the
main delivered sketch. It combines:

- WiFi HTTP endpoints for driving, steering, and probe servo control.
- Magnetic polarity sensing on `A0`.
- IR pulse counting and lambda classification.
- Ultrasound presence detection.
- RF packet and age decoding.
- `/data` JSON output for the web interface.

## Test sketches

`arduino/tests/` keeps earlier working sketches so that individual subsystems
can be tested in isolation:

- `control_final_worked` - drive, steering, and probe servo control.
- `monitor_worked_test` - sensor monitoring and rock classification.
- `eee_rover_inverted_rf_test` - RF receiver testing.
- `radio_freq_code` - earlier RF/control integration kept from the original repository.

## Network setup

The committed sketches use placeholder WiFi credentials:

```cpp
const char ssid[] = "YOUR_WIFI_SSID";
const char pass[] = "YOUR_WIFI_PASSWORD";
```

Set these locally before uploading to the board. Do not commit real network
passwords.
