# MESS BME280 gateway

Runs on a second ESP32 that stays plugged into the laptop by USB. It creates a WPA2 Wi-Fi hotspot (default name `MESS-BME280`, address `192.168.4.1`) for battery-powered BME280 sensor nodes. Each reading a node POSTs to `http://192.168.4.1/readings` is printed as one `MESS_BME_JSON {...}` line on the gateway's USB serial. The laptop bridge (`firmware/bme280_serial/server/bridge.py --port GATEWAY_PORT`) reads those lines, validates them, and feeds the dashboard's **Environment (BME280)** tab.

The laptop does not join this hotspot, so it can stay on its usual network. No sensor is wired to the gateway.

Set the hotspot name and a password of 8–63 characters in a git-ignored `sdkconfig.local` (or under **MESS BME280 gateway** in menuconfig). The gateway refuses to start an open network. Use the same values in the sensor node's `firmware/bme280_serial/sdkconfig.local`:

```
CONFIG_MESS_GW_SSID="MESS-BME280"
CONFIG_MESS_GW_PASSWORD="choose-a-password"
```

Build and flash from this folder in an ESP-IDF terminal:

```sh
idf.py set-target esp32
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local" build
idf.py -p PORT flash monitor
```

The monitor shows `Hotspot "MESS-BME280" on channel 6`, then `Node ... joined` and one `MESS_BME_JSON` line per second for each node. Close the monitor before starting the bridge; only one program can read the port. Up to four nodes can join. The gateway forwards printable ASCII bodies up to 512 bytes and leaves checking to the bridge.

Validation: on 2026-10-20 a classic ESP32 (ESP-IDF 6.1, macOS) running this gateway received one reading per second from a BME280 node about 30 cm away (RSSI about −25 dBm), and the dashboard plotted them. Range, battery life and multi-node operation are untested.
