#!/usr/bin/env python3
"""Local MESS BME280 bridge: receives readings over USB serial and/or Wi-Fi and serves them to the dashboard."""
import argparse
from collections import deque
import csv
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import threading
import time
from urllib.parse import urlparse, parse_qs

FIELDS = ['received_at', 'source', 'node_id', 'sequence', 'temperature_c', 'humidity_pct', 'pressure_hpa', 'rssi_dbm']
# Matches the printf in main/bme280_main.c; log lines and boot noise are ignored.
LINE = re.compile(r'Temperature: (-?\d+\.\d+) C \| Humidity: (\d+\.\d+) %RH \| Pressure: (\d+\.\d+) hPa')
# Printed by firmware/bme280_gateway for each Wi-Fi reading its hotspot receives.
GATEWAY = 'MESS_BME_JSON '


def in_range(temperature, humidity, pressure):
    # BME280 datasheet operating ranges; anything outside is corrupted, not a reading.
    return -40 <= temperature <= 85 and 0 <= humidity <= 100 and 300 <= pressure <= 1100


def parse(line, now=None):
    if GATEWAY in line:
        try:
            return validate(json.loads(line.split(GATEWAY, 1)[1]), now)
        except (ValueError, RecursionError):
            return None
    match = LINE.search(line)
    if not match:
        return None
    temperature, humidity, pressure = map(float, match.groups())
    if not in_range(temperature, humidity, pressure):
        return None
    now = now or datetime.now(timezone.utc)
    return dict(received_at=now.isoformat(), source='serial', node_id='', sequence=None,
                temperature_c=temperature, humidity_pct=humidity, pressure_hpa=pressure, rssi_dbm=None)


def validate(data, now=None):
    """Checks a Wi-Fi POST body from main/wifi_sender.c and returns the stored record."""
    keys = {'node_id', 'boot_id', 'sequence', 'temperature_c', 'humidity_pct', 'pressure_hpa', 'rssi_dbm'}
    if not isinstance(data, dict) or set(data) != keys:
        raise ValueError('Missing or unexpected fields')
    for key, length in [('node_id', 12), ('boot_id', 8)]:
        if not isinstance(data[key], str) or not re.fullmatch('[0-9a-f]{%d}' % length, data[key]):
            raise ValueError('Invalid ' + key)
    if type(data['sequence']) is not int or not 0 <= data['sequence'] <= 2**32-1:
        raise ValueError('Invalid sequence')
    if type(data['rssi_dbm']) is not int or not -127 <= data['rssi_dbm'] <= 0:
        raise ValueError('Invalid RSSI')
    values = [data[k] for k in ('temperature_c', 'humidity_pct', 'pressure_hpa')]
    if any(type(v) not in (int, float) for v in values) or not in_range(*values):
        raise ValueError('Reading out of range')
    now = now or datetime.now(timezone.utc)
    return dict(received_at=now.isoformat(), source='wifi', node_id=data['node_id'], sequence=data['sequence'],
                temperature_c=float(values[0]), humidity_pct=float(values[1]), pressure_hpa=float(values[2]),
                rssi_dbm=data['rssi_dbm'])


class Bridge(ThreadingHTTPServer):
    def __init__(self, address, port, baud, directory, keep=20000):
        self.serial_port, self.baud = port, baud
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.readings = deque(maxlen=keep)
        self.lock = threading.Lock()
        self.state = 'starting' if port else None
        super().__init__(address, Handler)

    def record(self, reading):
        with self.lock:
            self.readings.append(reading)
            csv_path = self.directory / 'bme280.csv'
            needs_header = not csv_path.exists() or csv_path.stat().st_size == 0
            with csv_path.open('a', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=FIELDS)
                if needs_header:
                    writer.writeheader()
                writer.writerow(reading)
        print(json.dumps(reading), flush=True)

    def read_serial(self):
        import serial
        while True:
            try:
                port = serial.Serial()
                port.port, port.baudrate, port.timeout = self.serial_port, self.baud, 1
                # Leave DTR/RTS released so opening the port does not hold the ESP32 in reset.
                port.dtr = port.rts = False
                port.open()
                self.state = 'connected'
                with port:
                    while True:
                        reading = parse(port.readline().decode(errors='replace'))
                        if reading:
                            self.record(reading)
            except (serial.SerialException, OSError) as e:
                self.state = 'serial error: %s' % e
                time.sleep(2)


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def reply(self, status, body):
        blob = json.dumps(body, allow_nan=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(blob)))
        self.end_headers()
        self.wfile.write(blob)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == '/snapshot':
            try:
                limit = max(1, min(50000, int(parse_qs(parsed.query).get('limit', ['20000'])[0])))
            except ValueError:
                self.reply(400, {'error': 'Invalid limit'}); return
            with self.server.lock:
                readings = list(self.server.readings)[-limit:]
            self.reply(200, {'port': self.server.serial_port, 'state': self.server.state, 'readings': readings})
        elif parsed.path == '/health':
            self.reply(200, {'status': 'ok', 'state': self.server.state})
        else:
            self.reply(404, {'error': 'Not found'})

    def do_POST(self):
        if self.path != '/readings':
            self.reply(404, {'error': 'Not found'}); return
        if self.headers.get('Content-Type', '').split(';')[0].strip() != 'application/json':
            self.reply(415, {'error': 'Use application/json'}); return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 1024:
                self.reply(413, {'error': 'Body must be 1..1024 bytes'}); return
            record = validate(json.loads(self.rfile.read(length)))
        except (ValueError, UnicodeError, TimeoutError, RecursionError):
            self.reply(400, {'error': 'Invalid reading payload'}); return
        try:
            self.server.record(record)
        except OSError:
            self.reply(500, {'error': 'Could not persist reading'}); return
        self.reply(201, {'stored': True})

    def log_message(self, fmt, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Serial device, e.g. /dev/cu.SLAB_USBtoUART or COM4 (omit for Wi-Fi only)')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--host', default='127.0.0.1', help='Use 0.0.0.0 so an ESP32 on Wi-Fi can reach this laptop')
    parser.add_argument('--http-port', type=int, default=8001)
    parser.add_argument('--output', default='data')
    args = parser.parse_args()
    if args.port:
        try:
            import serial  # noqa: F401
        except ImportError:
            raise SystemExit('pyserial is required for --port: python3 -m pip install pyserial')
    server = Bridge((args.host, args.http_port), args.port, args.baud, args.output)
    if args.port:
        threading.Thread(target=server.read_serial, daemon=True).start()
    print(f'BME280 bridge: serial {args.port or "off"}, Wi-Fi POST http://{args.host}:{args.http_port}/readings'
          f' | log: {(server.directory/"bme280.csv").resolve()}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
