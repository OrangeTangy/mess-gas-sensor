# R dashboard

Plain black-and-white UI, minimal CSS, built with R Shiny and base R plots.

## Start the homepage

Install R, then run from the repository root on macOS, Windows or Linux:

```sh
Rscript dashboard/install.R
Rscript dashboard/run.R
```

Open http://127.0.0.1:3838 and keep the terminal running. The initial preview is simulated and needs no ESP32. Synthetic values are labeled on screen and in CSV exports. See the [root setup guide](../README.md) for Python, Windows commands and live telemetry setup.

For VS Code, use the R extension `REditorSupport.r`; install `languageserver` for completion/diagnostics. R Shiny itself is documented by [Posit](https://shiny.posit.co/r/getstarted/shiny-basics/lesson1/).

## Connect real telemetry

Start `bash scripts/collector.sh --host 0.0.0.0` on the laptop and configure the ESP32 to POST to the laptop's LAN IPv4 address, port 8000, `/readings`. Switch the dashboard from **Simulated preview** to **Live collector**. A failed collector connection displays an error and no demo fallback. A reachable but empty collector displays an empty state.

The collector is a small Python standard-library service; all dashboard views, data summaries, and range calculations are R. This separation allows acquisition to continue when the dashboard closes. The collector defaults to loopback unless you explicitly request a LAN bind; it has no public-cloud dependency.

The dashboard refreshes every two seconds and loads the most recent 20,000 records across all nodes. “All loaded” means that bounded snapshot, not the whole historical database. The SQLite database retains the full history; CSV and JSONL are convenience exports. `GET /snapshot?limit=50000` can provide a larger bounded sample for a separate R analysis.

## Understand the metrics

- TVOC is in ppb; eCO₂ is in ppm and is an **estimate**, not measured CO₂.
- Charts use the collector's receipt time. They break lines at >3-second gaps and board reboot changes.
- Node state becomes OFFLINE after 10 seconds without a received record, for nodes present in the selected window. The last measured value can remain in the table alongside the offline state.
- RSSI is the node-to-parent radio signal in dBm. `-127` is treated as unavailable. It is not distance.
- Mesh layer describes depth from the root; layer 1 is the root. This is not a topology map, and it does not identify every intermediate relay.
- Delivery is `unique received sequences / (last sequence - first sequence + 1)`, summed separately per node and boot. It includes heartbeats/errors as transmitted telemetry, while gas statistics exclude invalid values.
- Delivery within a received span misses losses before the first or after the last received packet. It must be read together with offline age and fixed trial duration. It does not establish maximum range on its own.
- Duplicate HTTP retries are removed by the collector's node/boot/sequence key.

## Range-test workflow

1. Pick a node, give the test step a unique label, enter the measured distance from the gateway, and describe walls/relay placements.
2. In live mode, click **Start this test step**. Subsequent received packets for that node are annotated. Labels persist in the collector until replaced.
3. Let the placement settle and collect a fixed interval (for example five minutes); note the planned interval and any total outage separately. Arrival-time annotation can include already-queued packets, so exclude a settling period from precise analysis.
4. Move to the next placement, use a new label, and repeat. Change one variable at a time.
5. Compare the ledger and delivery-versus-distance points; export the data. The chart does not claim an optimal range automatically.

All distances in simulated mode are invented examples. The dashboard never infers meters from RSSI.

## Files and checks

- `app.R`: UI, refresh logic, live collector requests, charts and CSV export.
- `analytics.R`: normalization, deduplication, node summaries, delivery and range summaries, demo generator.
- `test_analytics.R`: sequence-gap, reboot, deduplication and offline-state checks.
- `run.R`: start Shiny on loopback port 3838 (`MESS_DASHBOARD_PORT` can override it).

Run analytics tests from this folder with `Rscript test_analytics.R`. The app uses only a small set of CSS rules for borders, spacing, monochrome text and responsive layout; plots use gray shades and distinct line styles.

## Does the SGP30 provide these readings?

| Displayed quantity | Actual source | Interpretation |
|---|---|---|
| TVOC (ppb) | SGP30 IAQ command, second returned word | Sensor-processed total VOC signal; not identification of one particular gas. |
| eCO₂ (ppm) | SGP30 IAQ command, first returned word | Sensor-computed CO₂-equivalent; not measured CO₂. |
| RSSI (dBm) | ESP32 Wi-Fi parent-link metadata | Network measurement, not a gas-sensor output. |
| Mesh layer | ESP-IDF mesh stack | Routing depth, not distance. |
| Delivery and offline status | R calculations from sequences and arrival times | Derived telemetry diagnostics. |
| Distance (m), notes and labels | Team-entered test annotations | Must be measured/recorded by your team. |

The firmware reads both gas values from the SGP30 command response; it does not calculate a real eCO₂ value from TVOC on the laptop. Each returned word's CRC is checked before values are accepted. Warm-up and read failures produce null gas values. Sensor identity and the manufacturer's on-chip self-test run at startup. These checks cannot establish calibration accuracy or replace a physical test.

The sensor model is identified as SGP30 in the project quickstart and milestones. The exact physical breakout has not been inspected and no hardware is attached. The currently visible preview is synthetic. Confirming actual behavior requires wiring the verified breakout, flashing the correct ESP32 target, observing a successful identity/self-test, waiting through startup, and recording real live samples.

Primary reference: [Sensirion SGP30 datasheet, air quality signals and measurement command](https://sensirion.com/file/datasheet_sgp30/).
