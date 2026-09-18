import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from gate0 import partitions,ota_entries
from host_contract import generate
from prepare_quickjs import prepare
from report import evaluate,events
from flash_ota0 import check_image


class Contracts(unittest.TestCase):
    def test_partition_geometry(self):
        data=struct.pack('<HBBII16sI',0x50aa,0,0x10,0x500000,0x400000,b'ota_0',0)+b'\xff'*4096
        self.assertEqual(partitions(data)['ota_0']['offset'],0x500000)

    def test_ota_crc_rejects_corruption(self):
        data=bytearray(b'\xff'*8192)
        crc=zlib.crc32(struct.pack('<I',1),0xffffffff)
        self.assertEqual(crc,0x4743989a)
        struct.pack_into('<I20sII',data,0,1,b'\xff'*20,2,crc)
        self.assertTrue(ota_entries(data)[0]['valid'])
        data[28]^=1
        self.assertFalse(ota_entries(data)[0]['valid'])

    def test_trusted_profile_hash(self):
        p=json.loads((Path(__file__).resolve().parents[1]/'pocket.host.json').read_text())
        output=generate(p)
        digest=hashlib.sha256(json.dumps(p,sort_keys=True,separators=(',',':')).encode()).digest()
        self.assertIn(','.join(map(str,digest)),output)
        self.assertNotIn('battery',p['capabilities'])

    def test_quickjs_unknown_source_refused(self):
        with self.assertRaises(ValueError):prepare(b'not the pinned runtime')

    def test_report_cannot_pass_without_measurements(self):
        result=evaluate([],{'mode':'pocket','bin_bytes':100})
        self.assertEqual(result['status'],'NOT-YET')
        self.assertIn('missing successful 2h soak',result['causes'])

    def test_native_measurements_never_claim_runtime_pass(self):
        result=evaluate([{'type':'memory','scenario':0,'time_us':1,'internal_free':100000,
            'internal_min':100000,'largest_internal_block':50000,'psram_free':8000000,'guest_heap_used':0}],
            {'mode':'native','bin_bytes':100})
        self.assertIn('not build 3',result['causes'])

    def test_serial_prefixes_not_measurements(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'log';p.write_text('I (0) boot\n{"type":"memory"}\npartial {\n')
            self.assertEqual(events(p),[{'type':'memory'}])

    def test_package_receipt_splits_js_and_package_bytes(self):
        from package_receipt import sections, js_bytes, receipt_for, default_budgets
        root=Path(__file__).resolve().parents[1]
        pocket=root/'evidence/variant-a-solid/minified.pocket'
        rec=receipt_for(pocket,transform='minify',original=None,
                        budgets=default_budgets(root),profile_id='ws183-harness')
        self.assertEqual(rec['packageBytes'],135272)
        self.assertEqual(rec['javascriptBytes'],61034)
        self.assertLess(rec['javascriptBytes'],rec['packageBytes'])
        self.assertEqual(rec['pakBytes'],72480)
        self.assertEqual(rec['profileId'],'ws183-harness')
        self.assertEqual(rec['budgets']['evalBudgetUs'],500000)
        info=sections(pocket.read_bytes())
        self.assertEqual(js_bytes(info['sections'][3]),61034)

    def test_spiffs_first_js_is_not_the_minified_package_size(self):
        rec=json.loads((Path(__file__).resolve().parents[1]/'evidence/variant-a-solid/spiffs-first.receipt.json').read_text())
        self.assertEqual(rec['packageBytes'],209768)
        self.assertEqual(rec['javascriptBytes'],135522)
        self.assertNotEqual(rec['javascriptBytes'],rec['packageBytes'])

    def test_non_app_image_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'bad.bin';p.write_bytes(b'\0'*256)
            with self.assertRaises(SystemExit):check_image(p)

    def test_repl_prompt_and_corrupt_lines(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'log'
            p.write_text('ws183> {"type":"memory"}\n'
                         'ws183> report\n'
                         '{"type":unexpected console output}\n'
                         'I (1) log {"type":"memory"}\n'
                         '{"type":"wifi","connected":true}\n')
            self.assertEqual(events(p),[{'type':'memory'},
                                       {'type':'wifi','connected':True}])


if __name__=='__main__':unittest.main()
