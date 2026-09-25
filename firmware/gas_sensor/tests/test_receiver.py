import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.request
import urllib.error

spec = importlib.util.spec_from_file_location('receiver', Path(__file__).parents[1]/'server/receiver.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
SAMPLE = dict(schema_version=1,node_id='001122334455',boot_id='aabbccdd',sensor='sgp30',
              sensor_serial='010203040506',sequence=1,uptime_ms=21000,eco2_ppm=450,tvoc_ppb=12,
              valid=True,status='ok',error_code=0,humidity_compensated=False,baseline_restored=False,transport='wifi',mesh_layer=0,rssi_dbm=-52,node_role='sensor')

class Tests(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(r.validate(SAMPLE), SAMPLE)

    def test_warmup(self):
        p = dict(SAMPLE,valid=False,status='warming_up',eco2_ppm=None,tvoc_ppb=None)
        self.assertEqual(r.validate(p),p)

    def test_bad_values(self):
        for change in [dict(eco2_ppm=True),dict(tvoc_ppb=-1),dict(eco2_ppm=float('nan')),
                       dict(valid=False),dict(node_id='=SUM(A1)'),dict(sequence=-1),
                       dict(schema_version=True),dict(eco2_ppm=399),dict(error_code=-1)]:
            with self.subTest(change=change),self.assertRaises(ValueError):
                r.validate(dict(SAMPLE,**change))

    def test_http_storage(self):
        with tempfile.TemporaryDirectory() as directory:
            server=r.Receiver(('127.0.0.1',0),directory)
            thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
            url=f'http://127.0.0.1:{server.server_port}'
            try:
                req=urllib.request.Request(url+'/readings',json.dumps(SAMPLE).encode(),
                                           {'Content-Type':'application/json'})
                with urllib.request.urlopen(req) as response:
                    self.assertEqual(response.status,201)
                # A retry must not produce a second stored reading.
                with urllib.request.urlopen(req) as response:
                    self.assertTrue(json.load(response)['duplicate'])
                trial=dict(node_id=SAMPLE['node_id'],trial_label='hallway-1',distance_m=15,trial_notes='one wall')
                trial_req=urllib.request.Request(url+'/trial',json.dumps(trial).encode(),{'Content-Type':'application/json'})
                with urllib.request.urlopen(trial_req) as response:
                    self.assertEqual(response.status,200)
                with urllib.request.urlopen(url+'/snapshot?limit=10') as response:
                    snapshot=json.load(response)['readings']
                    self.assertEqual(len(snapshot),1)
                records=(Path(directory)/'readings.jsonl').read_text().splitlines()
                self.assertEqual(len(records),1)
                self.assertEqual(json.loads(records[0])['eco2_ppm'],450)
                self.assertEqual(len((Path(directory)/'readings.csv').read_text().splitlines()),2)
                with urllib.request.urlopen(url+'/latest') as response:
                    self.assertEqual(json.load(response)['sequence'],1)
                req=urllib.request.Request(url+'/readings',b'{}',{'Content-Type':'application/json'})
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(req)
                self.assertEqual(error.exception.code,400)
                error.exception.close()
                self.assertEqual(len((Path(directory)/'readings.jsonl').read_text().splitlines()),1)
            finally:
                server.shutdown(); thread.join(); server.server_close()

if __name__=='__main__':
    unittest.main()
