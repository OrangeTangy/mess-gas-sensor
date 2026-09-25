"""Write non-secret build-only settings; never flash these as lab credentials."""
import sys
from pathlib import Path

mode = sys.argv[1]
if mode not in {"serial", "wifi", "mesh-sensor", "mesh-relay"}:
    raise SystemExit("Expected serial, wifi, mesh-sensor or mesh-relay")
lines = []
if mode != "serial":
    lines += ['CONFIG_MESS_WIFI_SSID="CI_BUILD_ONLY"',
              'CONFIG_MESS_WIFI_PASSWORD="not-a-real-network"']
if mode.startswith("mesh-"):
    lines += ['CONFIG_MESS_MESH=y', 'CONFIG_MESS_MESH_PASSWORD="ci-mesh-only"']
if mode == "mesh-relay":
    lines += ['CONFIG_MESS_SENSOR_ENABLED=n']
project = Path(__file__).resolve().parents[1] / "firmware" / "gas_sensor"
(project / "sdkconfig.ci").write_text("\n".join(lines) + "\n")
