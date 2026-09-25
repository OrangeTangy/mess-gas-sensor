# Gas-team mesh and range testing

This repository now includes optional **ESP-WIFI-MESH** transport in `main/mesh_transport.c`. The R dashboard remains the immediate deliverable; the firmware needs physical multi-board validation.

## Equipment

For a meaningful first multi-hop demonstration, use **three ESP32 boards**: one gas node, one intermediate relay, and one node near the router/server acting as root. One SGP30 is sufficient if the other boards are relay-only. A node with a sensor can also relay other nodes' data. Every remote board needs power; a USB power bank avoids being tied to the laptop.

The mesh automatically elects a root and chooses parent links. The elected root needs a route through the configured 2.4 GHz Wi-Fi router to the collector. Keep the laptop on that reachable LAN. Place nodes so a remote node cannot reach the router directly but can reach a relay; use the dashboard/logs to confirm mesh depth increased. There is no fixed-root guarantee or prescribed topology.

## Configure each ESP32

Use a confirmed supported board/target. This implementation is compiled for a classic ESP32 with ESP-IDF 6.1.

Run from the repository root:

```sh
cd firmware/gas_sensor
idf.py menuconfig
```

Under **MESS gas sensor**, set these consistently across all team boards:

- Enable ESP-WIFI-MESH multi-hop transport.
- Set the same test-router SSID and password.
- Set the router's actual 2.4 GHz channel (default config is 6). Use a test router with a known channel.
- Set the same gas-team mesh group ID (default 4).
- Set a mesh link password of 8–63 characters, the same on every node. The default is deliberately empty and must be filled in.
- Keep the same maximum layer count (default 6).
- Set the laptop receiver URL, for example `http://192.168.1.42:8000/readings` with the real address.
- Enable **SGP30 fitted to this node** only where a gas sensor is wired. Disable it for relay-only boards.

Build and flash each board with its own correct port. A relay-only board sends a clearly labeled heartbeat with null gas values and `sensor: "none"`; the dashboard does not treat it as a gas measurement.

Enable a LAN collector (`bash scripts/collector.sh --host 0.0.0.0`) and select Live collector in the R dashboard. It can remain reachable at loopback from the dashboard while the ESP32 reaches its LAN address.

## Routing and persistence

Non-root nodes send JSON to the root using the mesh stack's P2P transport. Intermediate nodes forward network traffic through the ESP-IDF mesh stack. The root receives messages into a 24-record queue and forwards them to the collector over HTTP, retrying up to three times. The collector uses a SQLite unique key on node/boot/sequence to suppress duplicate HTTP retries.

Sensor sampling runs in a separate higher-priority task. A one-record mailbox between sampling and transport keeps the most recent sample if transport stalls. Bounded queues and retries do not make this lossless: root changes, outages and queue pressure can drop records, and volatile queues are lost at reboot. Counts and gaps must be measured. The implementation does not yet provide source-to-database acknowledgments or durable on-node buffering.

The network can search for replacement parent links when a relay disappears; it can only recover if another usable radio path exists. Root election and multi-node reconnection must be validated on your hardware. Baseline timestamp persistence may be unavailable on non-root nodes without network time; their SGP30 measurements still run, with baseline-restored metadata indicating the state.

## Test maximum useful distance

“Maximum range” should mean a measured configuration meeting your agreed criterion, not the last point that delivered a single packet.

1. **Bench:** all nodes close together. Verify unique IDs, sensor/relay roles, valid gas values and logs for ten minutes.
2. **Direct link:** disable mesh for a baseline trial. At measured positions, collect fixed five-minute trials, recording gateway distance, walls, orientation and power.
3. **Relay extension:** enable mesh on all nodes. Keep adjacent hops within proven working coverage, then extend the chain. Record distances per hop separately in notes; the dashboard's distance field is distance from the gateway.
4. **Loss and outage:** use unique test labels, inspect sequence gaps, and record complete outages and test start/end times. Span delivery alone excludes trailing outage loss.
5. **Relay failure:** remove one relay's power, measure time to new received telemetry, then restore it. Repeat where an alternative path exists and where it does not.
6. **Load:** run all available gas/relay nodes. Look for increasing missing sequences or gateway queue-full logs.
7. **Duration:** run overnight before attempting a day/week test. Record resets, power-bank shutdown, node uptime and baseline handling.

A reasonable team-selected acceptance target might be at least 95% delivery over a fixed five-minute trial with no outage longer than ten seconds. This is a suggested test criterion, not a hardware guarantee or established project requirement. Use a full fixed interval when calculating final delivery; the dashboard's received-span metric is a quick diagnostic.

Do not extend the four I²C wires for distance. Move the complete battery-powered sensor+ESP32 assembly. Do not raise radio power beyond supported/regulatory limits. Antenna-equipped boards are a possible later hardware choice, but identify the provided boards and measure the current kit first.

Primary reference: [Espressif ESP-WIFI-MESH guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/esp-wifi-mesh.html).
