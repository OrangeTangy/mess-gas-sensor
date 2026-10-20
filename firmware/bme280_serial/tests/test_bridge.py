import importlib.util
from datetime import datetime, timezone
from pathlib import Path
import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

spec = importlib.util.spec_from_file_location('bridge', Path(__file__).parents[1]/'server/bridge.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)
NOW = datetime(2026, 10, 20, tzinfo=timezone.utc)
WIFI = dict(node_id='a4cf12000101', boot_id='0badf00d', sequence=7, temperature_c=24.6,
            humidity_pct=43.0, pressure_hpa=1019.4, rssi_dbm=-55)

class Tests(unittest.TestCase):
    def test_reading(self):
        self.assertEqual(b.parse('Temperature: 24.63 C | Humidity: 43.19 %RH | Pressure: 1019.45 hPa\r\n', NOW),
            dict(received_at=NOW.isoformat(), source='serial', node_id='', sequence=None,
                 temperature_c=24.63, humidity_pct=43.19, pressure_hpa=1019.45, rssi_dbm=None))

    def test_negative_temperature(self):
        self.assertEqual(b.parse('Temperature: -5.10 C | Humidity: 80.00 %RH | Pressure: 990.00 hPa', NOW)['temperature_c'], -5.1)

    def test_ignored(self):
        for line in ['I (251) BME280: BME280 found at 0x76 (chip ID 0x60)', '',
                     'E (300) BME280: Read measurement failed (-2); no valid reading',
                     'Temperature: 24.63 C | Humidity: 143.19 %RH | Pressure: 1019.45 hPa',
                     'Temperature: 24.63 C | Humidity: 43.19 %RH | Pressure: 19.45 hPa']:
            with self.subTest(line=line):
                self.assertIsNone(b.parse(line, NOW))

    def test_wifi(self):
        self.assertEqual(b.validate(WIFI, NOW), dict(received_at=NOW.isoformat(), source='wifi', node_id='a4cf12000101',
            sequence=7, temperature_c=24.6, humidity_pct=43.0, pressure_hpa=1019.4, rssi_dbm=-55))

    def test_wifi_rejected(self):
        for change in [dict(humidity_pct=101), dict(node_id='=SUM(A1)'), dict(sequence=-1), dict(sequence=True),
                       dict(temperature_c='24'), dict(rssi_dbm=5), dict(extra=1)]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                b.validate(dict(WIFI, **change), NOW)

    def test_gateway_line(self):
        self.assertEqual(b.parse('MESS_BME_JSON ' + json.dumps(WIFI) + '\r\n', NOW), b.validate(WIFI, NOW))
        for line in ['MESS_BME_JSON {"node_id":', 'MESS_BME_JSON ' + json.dumps(dict(WIFI, humidity_pct=200))]:
            with self.subTest(line=line):
                self.assertIsNone(b.parse(line, NOW))

    def test_post_and_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            server = b.Bridge(('127.0.0.1', 0), None, 115200, tmp)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            url = 'http://127.0.0.1:%d' % server.server_address[1]
            try:
                post = urllib.request.Request(url + '/readings', json.dumps(WIFI).encode(), {'Content-Type': 'application/json'})
                self.assertEqual(urllib.request.urlopen(post).status, 201)
                snapshot = json.load(urllib.request.urlopen(url + '/snapshot'))
                self.assertEqual(len(snapshot['readings']), 1)
                self.assertIsNone(snapshot['state'])
                bad = urllib.request.Request(url + '/readings', b'{}', {'Content-Type': 'application/json'})
                with self.assertRaises(urllib.error.HTTPError):
                    urllib.request.urlopen(bad)
            finally:
                server.shutdown(); server.server_close()

if __name__ == '__main__':
    unittest.main()
