# MESS BME280 serial collection

Derived from the ESP-IDF hello_world project. Prints real temperature (C), relative humidity (%RH), and pressure (hPa) once per second. **BME280 does not measure gas, TVOC, or CO2.** This is a separate local bench test, not the SGP30 gas dashboard protocol.

## Changed files compared with hello_world

- `main/bme280_main.c` replaces the hello-world print/countdown with I2C initialization, sensor identification, configuration, and a continuous read loop.
- `main/CMakeLists.txt` builds that file and links the I2C and Bosch driver components.
- `components/bme280/` contains Bosch's official calibration/compensation driver and its license.
- `main/Kconfig.projbuild` exposes SDA/SCL under MESS BME280 in menuconfig.
- Root CMakeLists names the application mess_bme280.

## Wiring assumptions — verify before flashing

For a classic ESP32 and a verified 3.3 V-compatible BME280 module: 3V3 to its approved supply input, GND to GND, GPIO21 to SDA, GPIO22 to SCL. The program probes addresses 0x76 and 0x77 and requires chip ID 0x60; BMP280 is not interchangeable because it lacks humidity.

Disconnect power before changing wires. Verify the actual module's supply pin and schematic. SDA/SCL need pull-ups to 3.3 V, usually already fitted on a breakout. For modules exposing CS/CSB and SDO, consult their wiring guide: CSB must be high for I2C, and SDO sets the address and must not float. Pin labels and onboard ties vary.

## Run on your teammate's Windows computer

These steps assume VS Code, the Espressif ESP-IDF extension, ESP-IDF 6.1, and USB access are already set up. No R, Python collector, Wi-Fi credentials, or extra sensor-library download is needed for this serial test; Bosch's driver is included.

### 1. Download the code

In a terminal in the folder where you keep projects:

```powershell
git clone https://github.com/OrangeTangy/mess-gas-sensor.git
cd mess-gas-sensor
```

If you already cloned it, open that checkout and run `git pull --ff-only` instead. Alternatively use GitHub **Code → Download ZIP**, then extract it.

### 2. Open the correct project

In VS Code choose **File → Open Folder** and select **mess-gas-sensor\firmware\bme280_serial**. Open the whole folder containing CMakeLists.txt, not just the C source file or the SGP30 gas_sensor folder.

Press **Ctrl+Shift+P** and run **ESP-IDF: Open ESP-IDF Terminal**. Check:

```powershell
idf.py --version
```

This project was built with ESP-IDF 6.1. If idf.py is not found, select the installed ESP-IDF version in the extension and reopen its terminal.

### 3. Configure the board and pins

For the classic ESP32-D0WD-V3 board used in our startup test:

```powershell
idf.py set-target esp32
idf.py menuconfig
```

Under **MESS BME280**, confirm SDA and SCL match the actual wiring (defaults GPIO21 and GPIO22). Save and exit. The supplied defaults select **4 MB flash**, matching our tested ESP32 board. For a different board, choose its actual target, exposed pins and flash size (Serial flasher config). Run set-target only for initial setup or when changing chips: it resets configuration.

### 4. Find the COM port, build and flash

Plug in the ESP32 using a USB data cable. In **Device Manager → Ports (COM & LPT)**, find its COM number. The board previously tested uses a CP2102N USB-to-UART bridge. If it appears as an unknown device, install the matching official Silicon Labs CP210x driver; other boards may use different bridges.

Close any other serial monitor using that port. Build, then flash and watch readings:

```powershell
idf.py build
idf.py -p COM4 flash monitor
```

**Replace COM4 with your actual port.** Select **UART** if VS Code asks for a flash method. The classic ESP32 uses the USB-to-serial bridge and does not need dfu-util. If connection stalls, follow the board's BOOT/EN procedure (commonly hold BOOT while the flashing tool connects, then release).

Flashing replaces the program currently stored on the board. Exit the monitor with **Ctrl+]**. Reopen it later without reflashing:

```powershell
idf.py -p COM4 monitor
```

### 5. Confirm readings

Expect a BME280 identification message (chip ID 0x60 at address 0x76 or 0x77), followed by readings about once per second. If the program says **No BME280 found**, disconnect power and check wiring, pull-ups, I2C mode, and whether the module is actually BME280 rather than BMP280. Correct wiring and reset the ESP32. Read errors do not produce fabricated values.

On macOS/Linux the same idf.py commands work in an activated ESP-IDF terminal; substitute the actual serial device path for COM4.

Successful output will look like this (illustrative values, not a hardware test):

```text
Temperature: 24.31 C | Humidity: 43.20 %RH | Pressure: 1008.54 hPa
```

Missing sensors or read failures produce errors, never simulated readings. After fixing wiring, reset the ESP32. Temperature can be influenced by nearby board heat. Pressure is local absolute pressure, not sea-level-corrected weather pressure.

## See readings on the dashboard graph

The R dashboard has an **Environment (BME280)** tab. It reads from a small bridge on the laptop (`server/bridge.py`, port 8001), which accepts readings two ways. The bridge keeps recent readings in memory and appends every reading to `data/bme280.csv`.

**USB cable.** Close any serial monitor first; only one program can read the port. Serial mode needs pyserial (`python3 -m pip install pyserial`).

```sh
python3 firmware/bme280_serial/server/bridge.py --port /dev/cu.SLAB_USBtoUART
```

**Wi-Fi with a gateway ESP32 (recommended).** A second ESP32 running [`firmware/bme280_gateway`](../bme280_gateway/README.md) creates its own hotspot and stays plugged into the laptop. The battery-powered sensor joins that hotspot and sends each reading to it, and the gateway passes it to the laptop over USB. The laptop stays on its normal network, eduroam included.

1. Put one shared hotspot name and password (8–63 characters) in both `firmware/bme280_gateway/sdkconfig.local` (`CONFIG_MESS_GW_SSID`, `CONFIG_MESS_GW_PASSWORD`) and `firmware/bme280_serial/sdkconfig.local` (`CONFIG_MESS_BME_WIFI_SSID`, `CONFIG_MESS_BME_WIFI_PASSWORD`). Git ignores both files. The sensor's default receiver URL, `http://192.168.4.1/readings`, is the gateway.
2. Build and flash each board over USB from its project folder (also set the sensor's SDA/SCL pins in menuconfig if they differ from 21/22):

```sh
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local" build
idf.py -p PORT flash
```

   `SDKCONFIG_DEFAULTS` only applies when `sdkconfig` is first created. If you already have one, delete it or change the values in menuconfig.
3. Leave the gateway plugged in and point the bridge at **its** port:

```sh
python3 firmware/bme280_serial/server/bridge.py --port /dev/cu.SLAB_USBtoUART4
```

4. Unplug the sensor and power it from a USB power bank (5 V into its USB socket, or a regulated supply to the 5V/VIN pin; never more than 3.6 V on the 3V3 pin). The dashboard banner reads **latest via Wi-Fi from …xxxx** with the link's signal strength.

**Wi-Fi straight to the laptop.** Alternatively, the sensor can join an ordinary 2.4 GHz network (such as a phone hotspot with **Maximize Compatibility** on) that the laptop is also on. Set the sensor's receiver URL to `http://LAPTOP_IP:8001/readings` (the laptop's address on that network, never `localhost`), and run the bridge with `--host 0.0.0.0` so the ESP32 can reach it. Enterprise networks such as eduroam do not work: the ESP32 cannot sign in, and they usually block device-to-device traffic.

The sensor sends only the newest reading and reconnects automatically after the hotspot drops or either board reboots. Readings taken while the link is down are dropped, not queued. Wi-Fi draws roughly 100–250 mA at peaks, so size the battery accordingly. Some power banks switch off at low draw; this load is normally enough to keep them on. Links are plain, unauthenticated HTTP; use only your own hotspot. Built firmware contains the Wi-Fi password, so do not share `.bin` files.

## Publish to the public website

Live site: **https://orangetangy.github.io/mess-gas-sensor/** (temperature, humidity and pressure graphs for 1 h / 6 h / 24 h, latest values, CSV download).

GitHub Pages only serves static files, so the computer running the bridge uploads the data. Add `--publish`:

```sh
python3 firmware/bme280_serial/server/bridge.py --port GATEWAY_PORT --publish
```

About every 6 minutes the bridge takes the last 24 hours from `data/bme280.csv` (one point per 10 s), adds the page in `site/`, and force-pushes both as a single commit to the `gh-pages` branch. `main` is untouched and `gh-pages` never builds up history. GitHub limits Pages to roughly 10 builds an hour, hence the interval; with Pages caching, the site runs about 5–20 minutes behind the local dashboard. Board hardware IDs are not published, but the readings are public.

Publishing uses that computer's git sign-in, so it must be able to push to this repository (a collaborator, signed in with `gh auth login` or Git Credential Manager). If a push fails, the bridge logs it and tries again next round; the local dashboard and CSV keep working.

## Running the gateway ("brain") on another computer

The sensor node does not need any computer. Once flashed, it runs from a power bank and keeps rejoining the gateway's hotspot. The gateway only needs USB power, but whichever computer it is plugged into receives the readings, so that computer must run the bridge:

1. Clone this repository and install Python 3 and pyserial (`python3 -m pip install pyserial`, or `py -3 -m pip install pyserial` on Windows).
2. Plug in the gateway and find its port (macOS: `ls /dev/cu.*`; Windows: Device Manager → Ports, e.g. `COM5`).
3. Run the bridge with that port, adding `--publish` if that computer should update the website.

Readings are stored in that computer's `data/bme280.csv`. If the gateway is only plugged into a charger, the sensor still connects but nothing is recorded or published. Keep the two boards within Wi-Fi range of each other; range has not been measured yet.

Validation: the exact application and driver sources compiled successfully for classic ESP32 with ESP-IDF 6.1 on macOS. Windows execution and physical BME280 readings have not yet been verified. The previous hello-world USB test verified the ESP32 board, not the attached sensor.
