# MESS Gas Sensor Group

**This repository is for the gas sensor group of MESS**, the Western Digital-sponsored RCOS project. It contains an ESP32/SGP30 gas node, an optional ESP32 mesh transport, a laptop telemetry collector, and a simple black-and-white **R Shiny homepage: Gas Sensor Dash**.

You can run the dashboard on macOS, Windows, or Linux **without plugging in an ESP32**. It starts with clearly labeled simulated readings. A teammate with USB access to the hardware can build/flash the C firmware and send real readings to the collector.

> Status: the dashboard and collector have been tested on a Mac, and the firmware has been compiled for a classic ESP32 using ESP-IDF 6.1. Physical sensor behavior, radio range, and multi-node recovery still need hardware testing. Simulated values are not hardware evidence.

## Using the BME280 right now?

For the team's simple **temperature, humidity and pressure** serial test, open [BME280 Windows setup and run instructions](firmware/bme280_serial/README.md). The project is in `firmware/bme280_serial`; its Bosch driver is included. Your teammate can build, flash and monitor it from their existing ESP-IDF setup.

**BME280 does not measure gas.** This standalone test prints to the USB serial monitor; it does not feed the SGP30 R dashboard. The gas-sensor instructions below apply to the SGP30 project.

## What runs where?

```text
SGP30 -- four short wires -- ESP32 with C firmware
                                   |
                          Wi-Fi / ESP32 relays
                                   |
                           Laptop collector
                                   |
                         R Shiny dashboard
```

- **ESP32:** runs C firmware built with ESP-IDF; reads the SGP30 and transmits telemetry.
- **Laptop collector:** a Python service that receives readings and saves them in SQLite, plus CSV/JSONL copies.
- **Laptop homepage:** an R Shiny app that displays live or simulated data at http://127.0.0.1:3838.
- **R code is not uploaded to the ESP32.** The homepage runs on the laptop, not on the sensor board.

## 1. Download the project

Install [Git](https://git-scm.com/downloads), then:

```sh
git clone https://github.com/OrangeTangy/mess-gas-sensor.git
cd mess-gas-sensor
```

Alternatively, use GitHub's **Code → Download ZIP**, extract it, and open the extracted folder in VS Code. Commands below assume you are in the repository's root directory unless stated otherwise.

## 2. Run the UI homepage — no hardware required

Install [R](https://cran.r-project.org/) for your operating system. RStudio is optional. Make sure `Rscript --version` works in a terminal; on Windows, add your R installation's `bin` folder to PATH or run these commands using the full path to `Rscript.exe`.

Install the dashboard packages once:

```sh
Rscript dashboard/install.R
```

Start the homepage:

```sh
Rscript dashboard/run.R
```

Open **http://127.0.0.1:3838** in a browser. Leave the terminal running. Stop it with **Ctrl+C**. If port 3838 is already occupied, close the earlier dashboard process before starting another.

The page starts in **Simulated preview**. Use it immediately on a Mac that cannot connect to an ESP32:

- **Live air quality:** TVOC and eCO₂ charts, node/time filters, summary statistics, CSV export.
- **Network health:** node status, last-seen age, parent-link RSSI, mesh depth and sequence-gap estimates.
- **Range experiments:** label live test steps, enter measured distance and placement notes, compare results.

Exports in preview mode are labeled `SIMULATED`. Switching to **Live collector** never substitutes fake readings when a collector is empty or unavailable.

If installing R packages fails because your machine lacks compilation dependencies, a prebuilt alternative is available with [Miniforge](https://github.com/conda-forge/miniforge):

```sh
conda env create -f dashboard/environment.yml
conda activate mess-r-dashboard
Rscript dashboard/run.R
```

Do not update the R environment while a dashboard process is running. Stop and restart R after package changes.

## 3. Run the collector for real readings

Install [Python 3](https://www.python.org/downloads/) (3.9 or newer). The collector uses only the standard library; no `pip install` is needed.

In a second terminal, from the repository root:

```sh
# macOS / Linux
python3 firmware/gas_sensor/server/receiver.py --host 0.0.0.0 --port 8000 --output data
```

```powershell
# Windows PowerShell
py -3 firmware/gas_sensor/server/receiver.py --host 0.0.0.0 --port 8000 --output data
```

Then select **Live collector** in the dashboard. If both programs are on the same laptop, leave **Collector address** as `http://127.0.0.1:8000`.

The ESP32 must use the **collector computer's LAN IPv4 address**, for example `http://192.168.1.42:8000/readings`. Do not put `localhost` or `127.0.0.1` in the ESP32 receiver URL: that would refer to the ESP32 itself.

You can use two computers: a teammate flashes the ESP32, while your Mac hosts the collector/dashboard. You can also run the collector on the teammate's laptop and set the Mac dashboard's **Collector address** to `http://THEIR_LAN_IP:8000`. Both computers and the ESP32 must have a reachable network path; the same SSID alone does not guarantee that.

Data is stored locally:

| File | Purpose |
|---|---|
| `data/telemetry.sqlite` | Authoritative store; deduplicates retries by node, boot and sequence |
| `data/readings.csv` | Convenient analysis copy |
| `data/readings.jsonl` | Line-oriented JSON copy |

The database does not live on GitHub. The repository excludes real telemetry, configuration credentials and compiled binaries.

`--host 0.0.0.0` permits LAN connections. This is an unauthenticated HTTP lab collector; use a trusted test network, permit inbound port 8000 in the collector machine's firewall when appropriate, and do not expose it directly to the public Internet. The Shiny UI itself stays on loopback by default.

## 4. Hardware needed

For one gas node:

- ESP32 development board and an **SGP30 breakout board**.
- A USB **data** cable matching the ESP32 socket.
- Solderless breadboard and at least four jumper wires.
- Header pins and soldering access if headers are not already fitted.
- Suitable Wi-Fi for testing; a classic ESP32 uses 2.4 GHz.
- USB power bank for untethered operation; confirm it remains on under the node's load.

A multimeter is useful. Breakout-specific pull-ups or level shifting may be needed; many commercial boards already include them. Verify the actual module before buying or wiring these parts.

**Do not assume the bare chip accepts 3.3 V.** The SGP30 chip uses 1.8 V; a regulated/level-shifted breakout may accept a different supply. GPIO21/SDA and GPIO22/SCL are this project's configurable defaults for a classic ESP32, not universal pin assignments for every ESP32 variant.

See [the wiring and flash guide](firmware/gas_sensor/README.md) before powering the hardware. A three-board mesh trial can use one sensor node, one relay, and a node near the router. A relay-only board does not need an SGP30.

## 5. Build and upload C firmware — on the computer with USB access

1. Install [VS Code](https://code.visualstudio.com/) and Espressif's **ESP-IDF** extension (`espressif.esp-idf-extension`).
2. Use **ESP-IDF: Open ESP-IDF Installation Manager** to install **ESP-IDF 6.1** and tools for the correct chip. Select that installation in the extension.
3. Open `firmware/gas_sensor` in VS Code and open an **ESP-IDF terminal**.
4. For a confirmed classic ESP32, run:

```sh
idf.py set-target esp32
idf.py menuconfig
```

Under **MESS gas sensor**, verify the GPIOs, keep **SGP30 fitted to this node** enabled, leave mesh disabled for the first test, and leave the SSID empty for serial-only operation.

```sh
idf.py build
idf.py -p YOUR_PORT flash monitor
```

Replace `YOUR_PORT` with the actual device port, such as `COM4` on Windows, `/dev/cu.usbserial-...` on macOS or `/dev/ttyUSB0` on Linux. On macOS, use `ls /dev/cu.*` before and after plugging in. Exit the serial monitor with **Ctrl+]**. Use a different ESP-IDF target and correct GPIOs if your board is not a classic ESP32; the provided build is not interchangeable across chips.

For wireless operation, set your Wi-Fi SSID/password and **Laptop receiver URL** in `menuconfig`, then:

```sh
idf.py -p YOUR_PORT build flash monitor
```

After readings arrive at the laptop, replace the USB-to-laptop connection with a USB power bank. The four short sensor wires stay attached. The code is already stored in the ESP32's flash memory and runs when powered.

For relays and longer-distance tests, see [MESH.md](firmware/gas_sensor/MESH.md). All mesh nodes need consistent router/channel/group/password settings. No guaranteed range is claimed.

## 6. VS Code setup

- Open the repository root to edit R, collector code and documentation together.
- Recommended extensions: **R** (`REditorSupport.r`) and **ESP-IDF** (`espressif.esp-idf-extension`).
- Optional R editing support: `Rscript dashboard/install.R --editor` installs `languageserver`.
- **Terminal → Run Task → MESS: R dashboard** starts the homepage if `Rscript` is on PATH.
- **MESS: Local collector** and **MESS: LAN collector for ESP32** start Python; install Python on PATH on Windows.
- For firmware, use the ESP-IDF terminal in `firmware/gas_sensor`; the extension's compiler environment is required.

On a Mac, `NoPermissions` opening a file under Documents can mean VS Code lacks Documents access. Check **System Settings → Privacy & Security → Files and Folders → Visual Studio Code → Documents Folder**, then quit/reopen VS Code.

## What the sensor actually reports

| Quantity | Source |
|---|---|
| TVOC, ppb | SGP30 processed total-VOC output |
| eCO₂, ppm | SGP30 CO₂-equivalent estimate; **not direct CO₂ measurement** |
| RSSI, dBm | ESP32 Wi-Fi parent link |
| Mesh layer | ESP-IDF routing metadata |
| Delivery/offline status | R calculations from sequence IDs and reception time |
| Distance, meters | Measured and entered by the team |

The firmware reads the sensor's returned words, validates CRCs, masks startup samples, and marks read errors. It does not invent live gas values. The SGP30 is not a certified gas alarm. Source: [Sensirion SGP30 datasheet](https://sensirion.com/file/datasheet_sgp30/).

## Milestones and limits

This gas-group implementation supports the MESS sequence: September sensor readings/wireless collection; October ESP32 relaying plus server/UI; November range, duration, load and recovery experiments. Its standalone transport must be agreed with the wider MESS team before integrating other sensor groups.

The sensor runs at one measurement per second in a separate task. Queues are bounded and volatile; outages, retries and root changes can lose records. The dashboard's delivery percentage measures gaps **inside received sequence spans**, not complete fixed-duration delivery. Check offline age, note test start/end times and record total outages. The UI loads the most recent 20,000 records, while the collector database retains the full history.

## Tests and project layout

```sh
# From the repository root; use py -3 instead of python3 on Windows if needed
python3 -m unittest discover -s firmware/gas_sensor/tests -p "test_*.py"
Rscript dashboard/install.R --tests
Rscript dashboard/test_analytics.R
Rscript dashboard/test_server.R
```

[Verification notes](VERIFICATION.md) distinguish software checks from hardware tests. An optional [GitHub Actions template](ci/checks.yml) covers software tests and four firmware build variants. To enable it, add that file as `.github/workflows/checks.yml` using GitHub’s web editor or a login with workflow permission. Automated runs are not currently enabled.

```text
dashboard/                    R Shiny homepage, analytics, package installer and tests
firmware/gas_sensor/           ESP-IDF project, SGP30 component and mesh transport
firmware/gas_sensor/server/    Python telemetry collector
scripts/                      Optional macOS/Linux launch helpers
.vscode/                      Portable task/extension recommendations
```

MIT licensed. The initial MESS repository license attribution is retained in [LICENSE](LICENSE). This repository is the gas group's working implementation, not an official Western Digital product.
