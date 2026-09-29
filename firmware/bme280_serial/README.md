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

Validation: the exact application and driver sources compiled successfully for classic ESP32 with ESP-IDF 6.1 on macOS. Windows execution and physical BME280 readings have not yet been verified. The previous hello-world USB test verified the ESP32 board, not the attached sensor.
