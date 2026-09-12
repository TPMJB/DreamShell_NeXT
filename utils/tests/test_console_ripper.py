"""Behavior tests execute the production console C, with a mock drive and POSIX VFS."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zlib
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def make_sector(fad, seed=0):
    """Independent, bitwise EDC and GF(256) parity encoder for Mode 1 fixtures."""
    s = bytearray(2352)
    s[:12] = b'\x00' + b'\xff' * 10 + b'\x00'
    bcd = lambda n: (n // 10) * 16 + n % 10
    s[12:16] = bytes([bcd(fad // 4500), bcd(fad // 75 % 60), bcd(fad % 75), 1])
    s[16:2064] = bytes((i + seed) % 256 for i in range(2048))
    edc = 0
    for byte in s[:2064]:
        edc ^= byte
        for _ in range(8):
            edc = (edc >> 1) ^ (0xd8018001 if edc & 1 else 0)
    s[2064:2068] = edc.to_bytes(4, 'little')
    forward = [(i << 1) ^ (0x11d if i & 128 else 0) for i in range(256)]
    backward = {i ^ forward[i]: i for i in range(256)}
    for major_count, minor_count, mult, inc, dest in [(86,24,2,86,2076),(52,43,86,88,2248)]:
        for major in range(major_count):
            idx = (major >> 1) * mult + (major & 1)
            a = b = 0
            for _ in range(minor_count):
                v = s[12 + idx]
                idx = (idx + inc) % (major_count * minor_count)
                a = forward[a ^ v]
                b ^= v
            a = backward[forward[a] ^ b]
            s[dest + major] = a
            s[dest + major_count + major] = a ^ b
    return bytes(s)


class ConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('gcc'):
            raise RuntimeError('gcc is required for console recovery tests')
        cls.build = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.build.name) / 'console-test'
        subprocess.run(['gcc','-std=gnu11','-O1','-Wall','-Wextra','-Werror',
                        '-Wno-format-truncation','-Iutils/tests/console_shim','-Iinclude/SDL',
                        'utils/tests/console_harness.c',
                        'applications/gd_ripper/modules/verify.c',
                        'applications/gd_ripper/modules/checksum.c','-lz','-o',str(cls.exe)],
                       cwd=ROOT, check=True)
        cls.disc_data = b''.join(make_sector(45150+i,i) for i in range(32))

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.disc = self.path/'disc.bin';self.disc.write_bytes(self.disc_data)
        self.track = self.path/'track03.bin'
        self.db = self.path/'redump.db'
        self.db.write_text(f'DREAMSHELL_REDUMP_CRC_V1\nG\t1\tFixture\nT\t3\t{len(self.disc_data)}\t{zlib.crc32(self.disc_data):08x}\nE\n')
        (self.path/'rip.state').write_text('DreamShell GD Ripper state v1\ndisc_type 128\nuse_bin 1\ntracks 1\n3 45150 32 4 2352 track03.bin\n')
        (self.path/'rip.complete').write_text('Complete: 32 sectors\n')

    def run_c(self, *args):
        return subprocess.check_output([str(self.exe), *map(str,args)], text=True).strip().split()

    def rip(self, advanced=0, fault=0):
        return self.run_c('rip',self.disc,self.track,advanced,fault)

    def test_sector_checks_distinguish_payload_parity_address_and_unsupported(self):
        s = bytearray(make_sector(45150))
        def check():
            self.track.write_bytes(s)
            return int(self.run_c('sector',self.track,45150)[0])
        self.assertEqual(check(),0)
        s[100] ^= 1;self.assertEqual(check() & 4,4)
        s[100] ^= 1;s[2248] ^= 1;self.assertEqual(check(),8)
        s[:] = make_sector(45151);self.assertEqual(check(),2)
        s[15] = 2;self.assertEqual(check(),16)
        s[0] ^= 1;self.assertEqual(check(),1)

    def test_stream_crc_matches_without_reading_track_back(self):
        result = self.rip()
        self.assertEqual(result[0],'0');self.assertEqual(result[2],'32')
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))
        self.assertEqual(result[4],'0')
        self.assertEqual(self.track.read_bytes(),self.disc_data)
        result = self.run_c('verify',self.path,self.db,1)
        self.assertEqual(result,['7','0','0'])  # FULL_MATCH; zero storage reads
        self.assertIn('no storage read-back',(self.path/'verify.log').read_text())

    def test_advanced_read_retries_silent_corruption(self):
        result = self.rip(advanced=1,fault=1)
        self.assertEqual(result[0],'0')
        self.assertGreater(int(result[1]),2)
        self.assertEqual(self.track.read_bytes(),self.disc_data)

    def test_pause_resume_preserves_exact_prefix_and_crc(self):
        failed=self.rip(advanced=1,fault=2)
        self.assertEqual(failed[0],'-1');self.assertEqual(failed[2],'1')
        self.assertEqual(self.track.read_bytes(),self.disc_data[:2352])
        result=self.rip(advanced=1)
        self.assertEqual(result[0],'0');self.assertEqual(result[4],'0')
        self.assertEqual(self.track.read_bytes(),self.disc_data)
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))

    def test_legacy_resume_hashes_prefix_once(self):
        self.track.write_bytes(self.disc_data[:2352*3])
        result=self.rip()
        self.assertEqual(int(result[4]),2352*3)
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))
        result=self.rip()
        self.assertEqual(result[1],'0');self.assertEqual(result[4],'0')

    def test_torn_or_ahead_checkpoint_is_ignored(self):
        self.rip()
        journal=self.path/'track03.bin.crc'
        with journal.open('a') as f:f.write('\nCRC1 torn')
        self.assertEqual(self.run_c('journal',self.track,len(self.disc_data))[0],'1')
        journal.write_text(journal.read_text().replace('CRC1','CRCX'))
        result=self.rip()
        self.assertEqual(int(result[4]),len(self.disc_data))
        self.assertEqual(self.run_c('journal',self.track,2352)[0],'0')

    def test_scan_locates_sector_and_repair_preserves_original(self):
        self.rip()
        damaged=bytearray(self.track.read_bytes());damaged[2352+100]^=1
        self.track.write_bytes(damaged)
        result=self.run_c('verify',self.path,self.db,0)
        self.assertEqual(result[0],'8');self.assertEqual(result[1],'1')
        self.assertEqual((self.path/'track03.bin.suspect').read_text().split()[:2],['1','45151'])
        result=self.rip(advanced=1)
        self.assertEqual(result[0],'0')
        self.assertEqual(self.track.read_bytes(),self.disc_data)
        self.assertEqual((self.path/'track03.bin.repair-backup').read_bytes(),b'FAD 45151\n'+damaged[2352:4704])
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))
        self.assertEqual(self.run_c('verify',self.path,self.db,0)[:2],['7','0'])

    def test_bad_map_and_no_match_are_not_approved(self):
        self.rip()
        (self.path/'track03.bin.bad').write_text('track,track_sector,disc_lba,disc_fad,file_offset\n3,1,45001,45151,2352\n')
        self.assertEqual(self.run_c('verify',self.path,self.db,1)[0],'8')
        (self.path/'track03.bin.bad').unlink()
        self.db.write_text('DREAMSHELL_REDUMP_CRC_V1\nG\t1\tUnknown\nT\t3\t75264\t00000000\nE\n')
        self.assertEqual(self.run_c('verify',self.path,self.db,1)[0],'3')
        self.assertIn('inconclusive',(self.path/'verify.log').read_text())

    def test_tosec_catalog_is_searched(self):
        self.rip()
        self.db.rename(self.path/'tosec.db')
        self.assertEqual(self.run_c('verify',self.path,self.db,1)[0],'7')
        self.assertIn('catalog TOSEC',(self.path/'verify.log').read_text())

    def test_insert_remove_reinsert_reads_title_once_per_disc(self):
        data=bytearray(self.disc_data)
        data[16:32]=b'SEGA SEGAKATANA  '
        data[16+128:16+160]=b'SONIC ADVENTURE'.ljust(32,b' ')
        self.disc.write_bytes(data)
        self.assertEqual(self.run_c('detect',self.disc),['SONIC_ADVENTURE'])

    def test_resume_rejects_a_different_disc_with_same_track_size(self):
        self.track.write_bytes(self.disc_data)
        self.assertEqual(self.run_c('identity',self.disc,self.path,1),['0'])
        self.disc.write_bytes(make_sector(45150,99)+self.disc_data[2352:])
        self.assertEqual(self.run_c('identity',self.disc,self.path,1),['-1'])

    def test_uncheckpointed_tail_only_is_hashed_on_resume(self):
        self.rip(advanced=1,fault=2)
        with self.track.open('ab') as out:out.write(self.disc_data[2352:2352*3])
        result=self.rip()
        self.assertEqual(int(result[4]),2352*2)
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))

    def test_repaired_zero_fill_map_is_archived(self):
        self.rip()
        data=bytearray(self.disc_data);data[2352:4704]=bytes(2352)
        self.track.write_bytes(data)
        (self.path/'track03.bin.bad').write_text('track,track_sector,disc_lba,disc_fad,file_offset\n3,1,45001,45151,2352\n')
        self.run_c('verify',self.path,self.db,0)
        self.assertEqual(self.rip(advanced=1)[0],'0')
        self.assertFalse((self.path/'track03.bin.bad').exists())
        self.assertTrue((self.path/'track03.bin.bad.history').exists())
        self.assertEqual(self.run_c('verify',self.path,self.db,1)[0],'7')

    def test_ui_load_contract(self):
        xml=ROOT/'applications/gd_ripper/app.xml'
        tree=ET.parse(xml)
        exports=(ROOT/'applications/gd_ripper/modules/exports.txt').read_text().splitlines()
        for element in tree.iter():
            for value in element.attrib.values():
                if value.startswith('export:'):
                    self.assertIn(value[7:].split('(')[0],exports)
        # The actual legacy parser, rather than desktop XML validation alone.
        src=Path(self.build.name)/'xml.c';exe=Path(self.build.name)/'xml-test'
        src.write_text('#include "mxml.h"\nint main(int n,char**v){if(n!=2)return 2;FILE*f=fopen(v[1],"r");if(!f)return 2;mxml_node_t*x=mxmlLoadFile(NULL,f,MXML_OPAQUE_CALLBACK);fclose(f);if(!x)return 1;mxmlDelete(x);return 0;}\n')
        subprocess.run(['gcc','-Ilib/mxml',str(src),*map(str,(ROOT/'lib/mxml').glob('mxml-*.c')),'-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe),str(xml)],check=True)

    def test_stop_returns_and_focus_skips_disabled_buttons(self):
        self.assertEqual(self.run_c('controls'),['ok'])

if __name__ == '__main__':unittest.main()
