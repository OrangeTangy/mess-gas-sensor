# MESS gas sensor firmware and wiring guide

This project reads an SGP30 from an ESP32 using **C and ESP-IDF**, prints structured readings, and sends them through direct Wi-Fi or optional ESP-WIFI-MESH to a Python laptop receiver. The receiver saves CSV and JSONL. Start with serial readings, then test wireless operation, then validate the optional mesh transport and integrate it with the team's shared firmware.

## What you need

| Item | Quantity for one gas node | Notes |
|---|---:|---|
| ESP32 development board | 1 | Already part of your kit; exact variant still needs checking. |
| SGP30 **breakout board** | 1 | Identify its manufacturer/model and supply pin before wiring. |
| USB **data** cable | 1 | Must match the ESP32 USB socket; a charge-only cable cannot flash firmware. |
| Solderless breadboard | 1 | Wide ESP32 boards sometimes need two boards side by side to expose pins. |
| Jumper wires | 4 minimum | Power, ground, SDA, SCL. Get a small assortment of male–male and male–female. |
| Header pins | As needed | Match the board's hole pitch; most hobby breakouts use 2.54 mm headers. |
| Soldering iron and solder | Shared access | Only needed if headers are not already soldered. |
| Laptop with VS Code | 1 | Install VS Code and the ESP-IDF extension. |
| 2.4 GHz Wi-Fi router or hotspot | 1 for Wi-Fi demonstration | Laptop and ESP32 must be allowed to communicate, not merely show the same SSID. |
| USB power bank and cable | 1 per moving node | Power the ESP32 through its USB input. Confirm the bank stays on and supports the board's peak draw. |
| Ventilated enclosure and mounting | Optional for bench; useful for moving | Secure wiring, admit room air, and keep the radio antenna clear of metal. |
| Multimeter | Useful shared tool | Check voltage, ground continuity, solder joints and shorts. |
| External I²C pull-ups / level shifting | **Only if the breakout requires them** | Check the board schematic first; many breakouts already include these. |

**Battery power is what removes the laptop cable after flashing.** ESP-IDF is the software framework; it does not remove the short wires between the gas sensor and ESP32. No extra radio module is needed for ordinary ESP32 Wi-Fi. More ESP32 relay nodes and power sources are needed for a mesh range demonstration; a relay need not carry its own gas sensor.

A bare SGP30 chip runs at 1.8 V and cannot be wired as though it were a regulated 3.3 V breakout. For example, the [Adafruit breakout](https://learn.adafruit.com/adafruit-sgp30-gas-tvoc-eco2-mox-sensor/pinouts) has a regulator and level shifting; that does **not** establish the capabilities of an unknown board. Do not buy additional conversion parts until you identify the supplied module.

For battery planning, measure the complete node's average power while transmitting. Estimate runtime as `usable battery Wh / measured average W`. Battery mAh alone is misleading when battery and USB voltages differ. Run a real-duration test; SGP30 operation is continuous, so periodically cutting sensor power is incompatible with preserving its normal measurement history.

## Wiring — verify the actual board first

The defaults below are for a **classic ESP32 development board** and a **verified 3.3 V-compatible SGP30 breakout**. They are not universal ESP32-C3/C5/S3 pin assignments.

| ESP32 | SGP30 breakout |
|---|---|
| 3V3 | Manufacturer-approved 3.3 V supply input, often labeled VIN |
| GND | GND |
| GPIO21 | SDA |
| GPIO22 | SCL |

1. Disconnect USB/battery power before assembling.
2. Seat the boards so power/ground pins do not short through breadboard rows.
3. Connect the four wires, keeping I²C wires short. Do not extend I²C across rooms.
4. Check the breakout supply input and its pull-up/level-shifting circuit. ESP32-side SDA/SCL must not be pulled to 5 V. Do not treat a breakout regulator's output as its input.
5. Confirm solid soldered headers, shared ground, and correctly connected breadboard power rails; some rails are split halfway.
6. For an appropriate level-shifted/3.3 V bus lacking pull-ups, 4.7 kΩ from each of SDA/SCL to 3.3 V is a typical short-bus starting point. Verify against your module; adding parallel pull-ups to boards that already have them can make resistance too low.
7. Configure different GPIO numbers in `idf.py menuconfig` if your board uses different exposed, output-capable pins.

The driver uses I²C address `0x58` at 100 kHz. Its internal ESP32 pull-ups are disabled because this project expects proper external/breakout pull-ups. Never attach two SGP30s with the same address to one unsegmented bus; use separate ESP32s or an I²C multiplexer.

## VS Code and ESP-IDF

Use the official [Espressif extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension). This is an ESP-IDF project: you do not need Arduino IDE, PlatformIO, an Arduino SGP30 library, or a separate Python package to read the sensor.

For a normal installation on a teammate's machine:

1. Install VS Code and the official Espressif ESP-IDF extension.
2. Open the command palette: **Cmd+Shift+P** on Mac or **Ctrl+Shift+P** on Windows/Linux.
3. Run **ESP-IDF: Open ESP-IDF Installation Manager**; install ESP-IDF **6.1** and its ESP32 tools.
4. Run **ESP-IDF: Select Current ESP-IDF Version** if needed, selecting that installation.
5. Open **this `firmware/gas_sensor` folder**, not just a single C file.
6. Open an ESP-IDF terminal. Run the commands below from this folder.

See the [official installation instructions](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html). Installing the extension alone does not install the compiler/toolchain.

## First run: sensor only

For a confirmed classic ESP32:

```sh
idf.py set-target esp32
idf.py menuconfig
idf.py build
```

In **MESS gas sensor**, verify SDA/SCL and leave Wi-Fi SSID empty. If your board is another chip, use its matching target (for example `esp32s3`) and install that target's tools; the compiled classic-ESP32 binary is not interchangeable.

Connect the ESP32 with a data cable and find its serial port:

```sh
# macOS
ls /dev/cu.*
```

Use the new USB port, such as `/dev/cu.SLAB_USBtoUART` or `/dev/cu.usbserial-...`, not `/dev/cu.wlan-debug`. Windows uses a port such as COM4 shown in Device Manager. A CP210x driver is appropriate only if the board actually uses that bridge; other boards use CH34x or native USB. Get a required driver from its chip manufacturer's official site.

```sh
idf.py -p YOUR_PORT flash monitor
```

Replace `YOUR_PORT` with the real port; do not type the placeholder literally. Exit the monitor using **Ctrl+]**. If auto-flashing fails, many classic ESP32 development boards require holding BOOT while the flashing tool connects; follow the board's own instructions.

Expected observations:

- A log reports SGP30 serial number and feature set after communication and self-test succeed.
- During startup, JSON uses `status: "warming_up"`, `valid: false`, and null measurement fields.
- After the startup interval, valid samples show `eco2_ppm` and `tvoc_ppb`.
- A missing sensor or corrupted transaction produces `sensor_error`, not invented readings.
- One JSON reading is produced roughly each second. Other ESP-IDF log lines also appear.

The SGP30's first 15 seconds produce fixed initialization values. The application masks the first 16 seconds conservatively. A TVOC reading of zero in ordinary room air can be legitimate; do not assume it means broken wiring. CO₂-equivalent is an algorithmic estimate, **not a direct CO₂ concentration measurement** and not a certified gas alarm. Source: [Sensirion datasheet](https://sensirion.com/file/datasheet_sgp30/).

## Wireless run: laptop receiver

1. Connect the laptop and ESP32 to a suitable lab Wi-Fi network. A classic ESP32 needs 2.4 GHz. Verify the exact board model.
2. Open a second terminal in this folder and run:

```sh
python3 server/receiver.py --host 0.0.0.0 --port 8000 --output data
```

The default receiver bind is loopback for local testing; the explicit `0.0.0.0` above accepts LAN connections. This is a lab HTTP receiver, without authentication/TLS. Use an appropriate test LAN. Do not expose it directly to the public Internet.

3. Find the laptop's Wi-Fi IPv4 address in macOS **System Settings → Wi-Fi → Details → TCP/IP**. Suppose it is `192.168.1.42`.
4. Run `idf.py menuconfig` and set:

```text
MESS gas sensor
  Wi-Fi SSID: your test network
  Wi-Fi password: your test password
  Laptop receiver URL: http://192.168.1.42:8000/readings
```

Use the laptop's real address. `localhost` on an ESP32 means the ESP32 itself. Do not use the example address unchanged. Credentials are stored in generated `sdkconfig` and firmware; the repository ignores configuration/build outputs. Do not commit or distribute configured build binaries containing real credentials.

5. Rebuild and flash with `idf.py -p YOUR_PORT build flash monitor`.
6. Confirm a JSON record arrives in the laptop terminal and files appear as `data/readings.csv` and `data/readings.jsonl`.
7. Disconnect USB, power the board using a USB power bank, and confirm readings resume after startup. You will lose the wired serial view while battery-powered; inspect the laptop receiver instead.
8. Walk to increasing distances and verify reception. Do not assign a guaranteed range before testing walls, interference, antenna orientation and power.

Useful local receiver checks:

```sh
curl http://127.0.0.1:8000/health
curl http://127.0.0.1:8000/latest
```

If serial readings work but Wi-Fi does not, check 2.4 GHz availability, the SSID/password, laptop firewall access for the receiver, AP client isolation, and laptop address changes. Campus Wi-Fi may require enterprise authentication or a captive portal; this project implements ordinary personal/open Wi-Fi, not campus enterprise login. A suitable test router/hotspot removes that uncertainty. Some hotspots also isolate clients, so verify connectivity rather than assuming.

## Moving far away and the mesh milestone

Current implementation:

```text
SGP30 -- four short wires -- ESP32 + power bank
                                  |
                                Wi-Fi
                                  |
                           router/access point
                                  |
                           laptop receiver
```

Optional mesh transport:

```text
remote gas node ~~ relay ESP32 ~~ another relay ~~ root/gateway ~~ server
```

ESP-IDF includes [ESP-WIFI-MESH](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/esp-wifi-mesh.html). Nodes relay data through neighboring nodes; a connected chain can extend coverage. Every hop still needs a working radio link. Different buildings do not automatically form a network simply because every node has Wi-Fi. Different Wi-Fi networks also require a reachable server/gateway and an agreed networking design.

The optional transport in `main/mesh_transport.c` uses ESP-IDF multi-hop mesh with automatic root election and root-to-server forwarding. Configure it using [MESH.md](MESH.md). Physical multi-node validation is still required. Agree on this protocol/board combination with the firmware group before integrating across teams. If choosing ESP-NOW instead, understand that ESP-NOW alone does not provide the team's complete mesh routing, duplicate suppression, TTL, retries and gateway logic.

The gas-driver component can remain unchanged during that integration. The existing sender selects direct Wi-Fi or the optional mesh transport; preserve the one-second sensor task when integrating other team code. A root/gateway can forward the same JSON schema to `/readings`. The current receiver uses arrival time for logs; preserve source timing and sequence numbers when adding queues or relays.

## What the code does

| File | Purpose |
|---|---|
| `components/sgp30/sgp30.c` | Portable C SGP30 command driver; per-word CRC checks, measurement timing, identity, self-test, baselines and humidity API. |
| `components/sgp30/include/sgp30.h` | Driver API and transport callbacks; all return codes documented in the enum. |
| `main/main.c` | ESP-IDF I²C adapter, sensor task, Wi-Fi sender, configuration, JSON, baseline NVS persistence. |
| `main/Kconfig.projbuild` | Pins, Wi-Fi credentials, server URL and time-server settings. |
| `server/receiver.py` | Standard-library HTTP receiver, schema validation, SQLite deduplication, CSV/JSONL logging and R snapshot API. |
| `tests/` | Driver protocol tests and real localhost HTTP/storage tests. |

The higher-priority sensor task reads once per second. A separate sender takes readings from a one-slot mailbox, so HTTP timeouts cannot hold up sensor sampling. This intentionally favors the newest reading: disconnected/slow-network samples can be dropped. This is **not lossless data collection**. Sequence gaps expose losses; SQLite is the receiver's authoritative, deduplicated store; CSV/JSONL are convenience copies. Add bounded buffering, acknowledgments and deduplication with the team when building reliable mesh delivery.

After repeated errors, the task retries identification and IAQ initialization. A scheduling gap beyond 1.5 seconds also restarts IAQ, rather than issuing a burst of missed commands. Warm-up and invalid samples are marked. A wedged electrical bus may still require a power cycle after correcting the wiring.

`node_id` identifies the ESP32, `sensor_serial` identifies its sensor, `boot_id` distinguishes resets, and `sequence` counts output records per boot. `uptime_ms` is monotonic board time, not UTC. `received_at` is the laptop's UTC arrival timestamp. Filter `valid == true` for analysis, and retain the quality flags.

## Baselines, humidity, and comparing buildings

The driver supports reading and restoring the sensor's internal baseline. Reading and writing baseline words use different word orders; the test suite checks this. This app conservatively waits **12 continuous hours** before its first baseline save, then saves hourly. This is an application policy, not a claim that every earlier reading is invalid.

A saved baseline is restored only for the same sensor serial number, with a known clock and age no more than seven days. If the clock is unavailable it skips persistence/restoration instead of assuming an old record is fresh. Network time synchronization is optional and requires the configured NTP server to be reachable. If time becomes available only after the startup window, the app does not inject a baseline into an established run. Serial-only use starts without restored baselines. These behaviors are grounded in the manufacturer's [driver baseline guidance](https://github.com/Sensirion/embedded-sgp/blob/master/sgp30/sgp30.h).

Humidity compensation needs a real external humidity reading. The default implementation reports `humidity_compensated: false`. When the BME280 teammate is ready, implement the `mess_get_absolute_humidity(float *g_m3)` hook in a separate C file and add it to `main/CMakeLists.txt`. Return false for missing/stale data; return a fresh measured **absolute** humidity value for compensation. The gas task then writes it to the SGP30. Do not pass relative humidity percent directly. Coordinate access if sharing the I²C bus; keep one owner for the SGP30 device's multi-step commands.

For meaningful building comparisons, log building/room context separately, co-locate nodes initially, use comparable measurement periods, and track whether humidity compensation and startup state match. A large eCO₂ value alone does not establish an actual CO₂ concentration or identify a specific pollutant. Avoid direct sprays/solvents or condensation on the sensor during testing; use normal ambient changes and document them.

## Completion checklist for your September contribution

- [ ] Record exact ESP32 and SGP30 breakout model; verify supply and GPIO assignments.
- [ ] Photograph/document assembled wiring; confirm no power shorts.
- [ ] Build and flash; capture sensor identity and successful self-test.
- [ ] Capture at least several minutes of post-startup serial measurements.
- [ ] Show the laptop receiving and storing readings over Wi-Fi.
- [ ] Demonstrate power-bank operation without a cable to the laptop.
- [ ] Disconnect Wi-Fi and confirm serial sampling continues; reconnect and confirm delivery resumes.
- [ ] Power down, disconnect the sensor, restart, and confirm explicit sensor errors. Power down again before rewiring.
- [ ] Measure packet/sequence gaps versus distance and walls; record the actual tested configuration.
- [ ] Hand off the component, schema, limitations, and logs to the team for mesh integration.

Later validation: run overnight, test baseline save/restore with a valid clock, integrate fresh BME280 compensation, test multiple nodes, and compare results with a reference instrument appropriate to the quantity being measured. Hardware checks remain required even when compilation and simulated-transport tests pass.

## References

- [Sensirion SGP30 datasheet](https://sensirion.com/file/datasheet_sgp30/)
- [ESP-IDF I²C API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html)
- [Repository setup and R homepage](../../README.md)
