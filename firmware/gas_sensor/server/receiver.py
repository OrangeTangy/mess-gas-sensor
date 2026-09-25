#!/usr/bin/env python3
"""Local MESS lab receiver: Python standard library only. No cloud services."""
import argparse
import csv
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import re
import sqlite3
from urllib.parse import urlparse, parse_qs

FIELDS = ['received_at', 'schema_version', 'node_id', 'boot_id', 'sensor',
          'sensor_serial', 'sequence', 'uptime_ms', 'eco2_ppm', 'tvoc_ppb',
          'valid', 'status', 'error_code', 'humidity_compensated', 'baseline_restored',
          'transport', 'mesh_layer', 'rssi_dbm', 'node_role', 'trial_label', 'distance_m', 'trial_notes']


def validate(data):
    if not isinstance(data, dict):
        raise ValueError('Expected a JSON object')
    data = dict(data)
    for key, value in dict(transport='wifi',mesh_layer=0,rssi_dbm=-127,node_role='sensor').items():
        data.setdefault(key,value)
    required = set(FIELDS) - {'received_at','trial_label','distance_m','trial_notes'}
    if set(data) != required:
        raise ValueError('Missing or unexpected payload fields')
    if type(data['schema_version']) is not int or data['schema_version'] != 1 or data['sensor'] not in ('sgp30','none'):
        raise ValueError('Unsupported schema or sensor')
    for key, length in [('node_id', 12), ('boot_id', 8), ('sensor_serial', 12)]:
        if not isinstance(data[key], str) or not re.fullmatch('[0-9a-f]{%d}' % length, data[key]):
            raise ValueError('Invalid ' + key)
    for key in ['sequence', 'uptime_ms']:
        if type(data[key]) is not int or not 0 <= data[key] <= 2**53-1:
            raise ValueError('Invalid ' + key)
    for key in ['valid', 'humidity_compensated', 'baseline_restored']:
        if type(data[key]) is not bool:
            raise ValueError('Invalid ' + key)
    if type(data['error_code']) is not int or not -4 <= data['error_code'] <= 0:
        raise ValueError('Invalid error code')
    if data['status'] not in ('ok', 'warming_up', 'sensor_error', 'relay'):
        raise ValueError('Invalid status')
    if data['valid'] != (data['status'] == 'ok'):
        raise ValueError('Status/valid mismatch')
    if (data['status'] == 'sensor_error') != (data['error_code'] != 0):
        raise ValueError('Status/error mismatch')
    for key, minimum in [('eco2_ppm', 400), ('tvoc_ppb', 0)]:
        value = data[key]
        if data['valid']:
            if type(value) is not int or not minimum <= value <= 60000:
                raise ValueError('Invalid ' + key)
        elif value is not None:
            raise ValueError('Invalid readings must be null')
    if data['transport'] not in ('wifi','mesh','serial') or data['node_role'] not in ('sensor','relay'):
        raise ValueError('Invalid transport/role')
    if type(data['mesh_layer']) is not int or not -1 <= data['mesh_layer'] <= 25:
        raise ValueError('Invalid mesh layer')
    if type(data['rssi_dbm']) is not int or not -127 <= data['rssi_dbm'] <= 0:
        raise ValueError('Invalid RSSI')
    if (data['node_role']=='relay') != (data['sensor']=='none' and data['status']=='relay'):
        raise ValueError('Invalid relay payload')
    if data['node_role']=='sensor' and data['sensor']!='sgp30':
        raise ValueError('Invalid sensor role')
    return dict(data)


class Receiver(HTTPServer):
    def __init__(self, address, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.latest = None
        self.db = sqlite3.connect(self.directory/'telemetry.sqlite',check_same_thread=False)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.execute('CREATE TABLE IF NOT EXISTS readings (id INTEGER PRIMARY KEY, node TEXT, boot TEXT, seq INTEGER, payload TEXT, UNIQUE(node,boot,seq))')
        self.db.execute('CREATE TABLE IF NOT EXISTS trials (node TEXT PRIMARY KEY, payload TEXT)')
        self.db.commit()
        super().__init__(address, Handler)


    def server_close(self):
        super().server_close()
        self.db.close()


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
        parsed=urlparse(self.path)
        if parsed.path == '/snapshot':
            try:
                limit=max(1,min(50000,int(parse_qs(parsed.query).get('limit',['20000'])[0])))
            except ValueError:
                self.reply(400,{'error':'Invalid limit'}); return
            rows=self.server.db.execute('SELECT payload FROM readings ORDER BY id DESC LIMIT ?', (limit,)).fetchall()
            self.reply(200,{'readings':[json.loads(row[0]) for row in reversed(rows)]})
        elif self.path == '/health':
            self.reply(200, {'status': 'ok'})
        elif self.path == '/latest':
            self.reply(200, self.server.latest)
        else:
            self.reply(404, {'error': 'Not found'})

    def do_POST(self):
        if self.path not in ('/readings','/trial'):
            self.reply(404, {'error': 'Not found'})
            return
        if self.headers.get('Content-Type', '').split(';')[0].strip() != 'application/json':
            self.reply(415, {'error': 'Use application/json'})
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 4096:
                self.reply(413, {'error': 'Body must be 1..4096 bytes'})
                return
            raw = self.rfile.read(length)
            if len(raw) != length:
                raise ValueError('Incomplete request body')
            body=json.loads(raw)
            if self.path=='/trial':
                if not isinstance(body,dict) or set(body)!={'node_id','trial_label','distance_m','trial_notes'}:
                    raise ValueError('Invalid trial')
                if not isinstance(body['node_id'],str) or not re.fullmatch('[0-9a-f]{12}',body['node_id']):
                    raise ValueError('Invalid node')
                for key in ('trial_label','trial_notes'):
                    if not isinstance(body[key],str) or len(body[key])>200 or any(ord(c)<32 for c in body[key]):
                        raise ValueError('Invalid text')
                    # Neutralize spreadsheet formula prefixes in exported annotation columns.
                    if body[key].startswith(('=','+','-','@')): body[key]="'"+body[key]
                if type(body['distance_m']) not in (int,float) or not 0<=body['distance_m']<=100000:
                    raise ValueError('Invalid distance')
                self.server.db.execute('INSERT OR REPLACE INTO trials VALUES (?,?)',(body['node_id'],json.dumps(body)))
                self.server.db.commit()
                self.reply(200,{'saved':True}); return
            record = validate(body)
        except (ValueError, UnicodeError, TimeoutError, RecursionError):
            self.reply(400, {'error': 'Invalid reading payload'})
            return
        trial_row=self.server.db.execute('SELECT payload FROM trials WHERE node=?',(record['node_id'],)).fetchone()
        trial=json.loads(trial_row[0]) if trial_row else dict(trial_label='',distance_m=None,trial_notes='')
        trial.pop('node_id',None)
        record = {'received_at': datetime.now(timezone.utc).isoformat(), **record, **trial}
        try:
            result=self.server.db.execute('INSERT OR IGNORE INTO readings(node,boot,seq,payload) VALUES (?,?,?,?)',
                (record['node_id'],record['boot_id'],record['sequence'],json.dumps(record,allow_nan=False)))
            self.server.db.commit()
            if result.rowcount==0:
                self.reply(200,{'stored':True,'duplicate':True}); return
            # SQLite is authoritative and deduplicated; CSV/JSONL are analysis copies.
            with (self.server.directory / 'readings.jsonl').open('a') as f:
                f.write(json.dumps(record, allow_nan=False) + '\n')
            csv_path = self.server.directory / 'readings.csv'
            needs_header = not csv_path.exists() or csv_path.stat().st_size == 0
            with csv_path.open('a', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=FIELDS)
                if needs_header:
                    writer.writeheader()
                writer.writerow(record)
        except (OSError,sqlite3.Error):
            self.reply(500, {'error': 'Could not persist reading'})
            return
        self.server.latest = record
        print(json.dumps(record), flush=True)
        self.reply(201, {'stored': True})

    def log_message(self, fmt, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='127.0.0.1', help='Use 0.0.0.0 for the ESP32 lab test')
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--output', default='data')
    args = parser.parse_args()
    server = Receiver((args.host, args.port), args.output)
    print(f'Receiver: http://{args.host}:{args.port} | logs: {server.directory.resolve()}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
