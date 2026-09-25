# Verification and hardware handoff

Software checks performed on macOS on September 25, 2026:

- Python collector: four passing tests covering validation, HTTP reception, SQLite persistence/deduplication, trial annotations and snapshots.
- R analytics: passing checks for sequence gaps, duplicate records, reboots, offline status and range summaries.
- R Shiny server: passing checks for simulated preview, exported CSV contents/source labels and unavailable live-collector behavior.
- Portable SGP30 C driver: passing protocol checks with compiler warnings treated as errors, AddressSanitizer and UndefinedBehaviorSanitizer. These use simulated I²C responses, not a physical sensor.
- Classic ESP32 firmware: serial and mesh sensor builds passed during development with ESP-IDF 6.1; a fresh build of this packaged project also passed with mesh relay settings and nonempty build-only network credentials. The optional CI template covers serial, direct Wi-Fi, mesh sensor and mesh relay variants.

The software was exercised locally with R 4.5.3, Shiny 1.14.0, jsonlite and curl. Windows/Linux instructions are portable but have not been manually tested on a Windows computer. The GitHub Actions template is provided but not enabled: the publishing login did not have workflow scope. No hosted CI result is claimed.

## Still required on hardware

No ESP32 was connected for this handoff. Nothing has been flashed or measured on a physical SGP30. Before treating the project as a working deployment:

1. Record the ESP32 target and breakout model; verify supply voltage, signal levels and GPIO wiring.
2. Build and flash on a computer with a working USB data connection.
3. Confirm sensor identity/self-test, startup flags and valid serial readings.
4. Confirm real packets reach the collector and appear in Live collector mode; export and inspect them.
5. Test battery power, Wi-Fi outages/reconnection and long-duration sampling.
6. Test mesh routing, relay-only nodes, root changes and recovery using multiple boards.
7. Measure distance and packet loss under documented conditions; there is no guaranteed range.
8. Validate baseline restoration and humidity compensation separately when the required time/humidity sources are available.

Compilation does not verify electrical compatibility, sensor accuracy or radio reliability. The demo's gas values and distances are synthetic. eCO₂ is the SGP30 equivalent estimate, not a direct CO₂ measurement.
