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
                        'applications/gd_ripper/modules/checksum.c',
                        'applications/gd_ripper/modules/recovery.c',
                        'applications/gd_ripper/modules/readback.c','-lz','-o',str(cls.exe)],
                       cwd=ROOT, check=True)
        cls.disc_data = b''.join(make_sector(45150+i,i) for i in range(32))
        cls.transition_data = b''.join(make_sector(150+i,i) for i in range(300))
        cls.transition_data += (bytes(range(256))*((676*2352+255)//256))[:676*2352]

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

    def test_default_destination_creates_games_and_preserves_existing_rips(self):
        self.assertEqual(self.run_c('default-destination',self.path),['/sd/Games|0|1'])
        game=self.path/'Games'/'My disc'; game.mkdir()
        track=game/'track03.bin'; track.write_bytes(b'keep this rip')
        self.assertEqual(self.run_c('default-destination',self.path),['/sd/Games|0|1'])
        self.assertEqual(track.read_bytes(),b'keep this rip')

    def test_default_destination_never_overwrites_a_file_named_games(self):
        target=self.path/'Games'; target.write_bytes(b'keep this file')
        self.assertEqual(self.run_c('default-destination',self.path),['/sd/Games|-1|0'])
        self.assertEqual(target.read_bytes(),b'keep this file')

    def test_destination_uses_ide_cf_sd_and_pc_games_folders(self):
        for device,expected in [('both','ide'),('ide','ide'),('sd','sd'),('pc','pc')]:
            with self.subTest(device=device):
                root=self.path/device; root.mkdir()
                self.assertEqual(self.run_c('default-destination',root,device),
                                 [f'/{expected}/Games|0|1'])
                self.assertTrue((root/'Games').is_dir())
        for device in ('sd','ide','pc'):
            with self.subTest(button=device):
                self.assertEqual(self.run_c('device-destination',self.path/device,device),
                                 [f'/{device}/Games|1'])

    def transition(self, scenario='append', resume=False):
        data=self.transition_data
        self.disc.write_bytes(data)
        if resume:
            (self.path/'track01.bin').write_bytes(data[:300*2352])
        result=subprocess.check_output([str(self.exe),'transition',str(self.path),str(self.disc),scenario],text=True)
        return result.strip().split('|'),data

    def test_track1_to_audio_transition_without_append_flag_support(self):
        result,data=self.transition()
        self.assertEqual(result[0:2],['0','826'])
        self.assertEqual((self.path/'track01.bin').read_bytes(),data[:705600])
        self.assertEqual((self.path/'track02.raw').read_bytes(),data[450*2352:])
        log=(self.path/'rip.log').read_text()
        self.assertIn('Starting track 1',log)
        self.assertIn('Starting track 2',log)
        self.assertIn('Track 2 completed successfully',log)
        self.assertTrue((self.path/'track01.bin.crc').exists())
        self.assertTrue((self.path/'track02.raw.crc').exists())
        self.assertEqual(list(self.path.glob('rip-io-*.tmp')),[])

    def test_completed_track1_without_crc_resumes_at_track2(self):
        result,data=self.transition(resume=True)
        self.assertEqual(result[0:2],['0','826'])
        self.assertEqual(result[2],'33')  # Only the 526-sector audio track reaches the drive.
        self.assertEqual((self.path/'track01.bin').read_bytes(),data[:705600])
        self.assertIn('already complete',(self.path/'rip.log').read_text())

    def test_old_core_reopen_failure_is_caught_before_reading_disc(self):
        previous=(self.path/'rip.state').read_bytes()
        result,_=self.transition('legacy')
        self.assertEqual(result[0:4],['-1','0','0','Storage reopen failed'])
        self.assertIn('DS_CORE.BIN',result[4])
        self.assertFalse((self.path/'track01.bin').exists())
        self.assertEqual((self.path/'rip.state').read_bytes(),previous)
        self.assertEqual(list(self.path.glob('rip-io-*.tmp')),[])

    def test_crc_save_failure_reports_storage_after_full_track1(self):
        result,data=self.transition('crc')
        self.assertEqual(result[0:2],['-1','300'])
        self.assertEqual(result[3],'CRC checkpoint failed')
        self.assertEqual((self.path/'track01.bin').read_bytes(),data[:705600])
        self.assertIn('CRC checkpoint failed',(self.path/'rip.log').read_text())
        self.assertFalse((self.path/'track02.raw').exists())

    def test_audio_read_failure_names_track_and_fad(self):
        result,_=self.transition('audio-read')
        self.assertEqual(result[0:2],['-1','300'])
        self.assertEqual(result[3],'Sector retries exhausted')
        self.assertIn('Track 2, FAD 600',result[4])
        self.assertEqual((self.path/'track02.raw').stat().st_size,0)

    def test_sector_mode_failure_reinitializes_and_retries(self):
        result,_=self.transition('mode')
        self.assertEqual(result[0:2],['0','826'])
        self.assertIn('attempt 2/3 failed',(self.path/'rip.log').read_text())

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
        self.assertIn('sector_scan NOT_RUN',(self.path/'verify.log').read_text())

    def recovery_fixture(self, indices=(1, 3, 8)):
        self.rip()
        data = bytearray(self.disc_data)
        for index in indices:
            data[index*2352:(index+1)*2352] = bytes(2352)
        self.track.write_bytes(data)
        journal = self.path/'track03.bin.crc'
        tag = journal.read_text().split()[1]
        body = f'CRC1 {tag} {len(data)} {zlib.crc32(data):08x}'
        journal.write_text(f'\n{body} {zlib.crc32(body.encode()):08x}\n')
        rows = ['track,track_sector,disc_lba,disc_fad,file_offset\n']
        rows += [f'3,{i},{45000+i},{45150+i},{2352*i}\n' for i in indices]
        (self.path/'track03.bin.bad').write_text(''.join(rows))
        return data

    def recover(self, fault=0, stop=0, kind=4, passes=3):
        return subprocess.check_output([str(self.exe),'recover',str(self.disc),str(self.track),
            str(fault),str(stop),str(kind),str(passes)], text=True).strip().split('|')

    def test_replacement_crc_includes_large_suffix_and_many_patches(self):
        data = bytearray(self.disc_data)
        crc = zlib.crc32(data)
        for index in (0, 31, 12, 7, 19):
            before = bytes(data[index*2352:(index+1)*2352])
            after = make_sector(45150+index,99+index)
            crc = int(self.run_c('replace-crc',f'{crc:08x}',f'{zlib.crc32(before):08x}',
                f'{zlib.crc32(after):08x}',len(data)-(index+1)*2352)[0],16)
            data[index*2352:(index+1)*2352] = after
            self.assertEqual(crc,zlib.crc32(data))
        # Composition plus the inequality checks that suffix lengths beyond
        # 32 bits are not truncated. Small patches use Python's zlib as oracle.
        a = self.run_c('replace-crc','0','12345678','0',2**32+2345)[0]
        b = self.run_c('replace-crc','0','12345678','0',2**32)[0]
        b = self.run_c('replace-crc','0',b,'0',2345)[0]
        self.assertEqual(a,b)
        self.assertNotEqual(a,self.run_c('replace-crc','0','12345678','0',2345)[0])

    def test_first_pass_continues_past_bad_sector_and_never_approves_hole(self):
        result = self.run_c('firstpass',self.disc,self.track,1,2)
        self.assertEqual(result[0],'0')
        self.assertEqual(result[2],'32')
        data = self.track.read_bytes()
        self.assertEqual(data[:2352],self.disc_data[:2352])
        self.assertEqual(data[2352:4704],bytes(2352))
        self.assertEqual(data[4704:],self.disc_data[4704:])
        self.assertEqual(self.run_c('verify',self.path,self.db,1)[0],'8')

    def test_targeted_recovery_crc_matches_without_whole_file_readback(self):
        self.recovery_fixture()
        result = self.recover()
        self.assertEqual(result[:3],['0','3','0'])
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))
        self.assertEqual(result[4],'3')
        self.assertLess(int(result[5]),len(self.disc_data)//2)
        self.assertEqual(self.track.read_bytes(),self.disc_data)
        self.assertFalse((self.path/'track03.bin.bad').exists())
        self.assertEqual(self.run_c('verify',self.path,self.db,1)[0],'7')

    def test_recovery_sweeps_past_stubborn_sector_and_only_retries_remaining(self):
        self.recovery_fixture()
        result = self.recover(fault=1)
        self.assertEqual(result[:3],['0','2','1'])
        self.assertEqual(result[4],'5') # stubborn sector gets 3 tries; others one.
        self.assertEqual(self.run_c('verify',self.path,self.db,1)[0],'8')
        result = self.recover()
        self.assertEqual(result[:3],['0','1','0'])
        self.assertEqual(result[4],'1')
        self.assertEqual(self.track.read_bytes(),self.disc_data)
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))

    def test_recovery_pass_budget_can_recover_on_later_pass(self):
        self.recovery_fixture()
        result = self.recover(fault=2)
        self.assertEqual(result[:3],['0','3','0'])
        self.assertEqual(result[4],'5')

    def test_recovery_stop_preserves_successful_sector_and_resumes_only_others(self):
        self.recovery_fixture()
        result = self.recover(stop=1)
        self.assertEqual(result[:3],['-1','1','2'])
        self.assertTrue((self.path/'track03.bin.bad').exists())
        self.assertFalse((self.path/'track03.bin.crc').exists())
        result = self.recover()
        self.assertEqual(result[:3],['0','2','0'])
        self.assertEqual(result[4],'2')
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))

    def test_torn_recovery_write_is_reconciled_from_baseline_on_restart(self):
        damaged = self.recovery_fixture()
        result = self.recover(fault=4)
        self.assertEqual(result[0],'-1')
        self.assertIn('write/read-back',result[6])
        self.assertNotEqual(self.track.read_bytes(),damaged)
        self.assertFalse((self.path/'track03.bin.crc').exists())
        self.assertIn(self.run_c('verify',self.path,self.db,1)[0],['-1','8'])
        result = self.recover()
        self.assertEqual(result[:3],['0','3','0'])
        self.assertEqual(self.track.read_bytes(),self.disc_data)
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))

    def test_audio_requires_agreement_and_resume_preserves_confirmed_audio(self):
        self.recovery_fixture()
        result = self.recover(kind=0,fault=3)
        self.assertEqual(result[:3],['0','0','3'])
        result = self.recover(kind=0,stop=1)
        self.assertEqual(result[:3],['-1','1','2'])
        result = self.recover(kind=0)
        self.assertEqual(result[:3],['0','2','0'])
        self.assertEqual(result[4],'4')
        self.assertEqual(int(result[3],16),zlib.crc32(self.disc_data))

    def test_torn_audio_confirmation_requires_new_reads(self):
        self.recovery_fixture()
        self.recover(kind=0,stop=1)
        journal = self.path/'track03.bin.recovery-audio'
        journal.write_bytes(journal.read_bytes()[:-4])
        result = self.recover(kind=0)
        self.assertEqual(result[:3],['0','3','0'])
        self.assertEqual(result[4],'6')

    def test_corrupt_published_baseline_stops_before_patching(self):
        self.recovery_fixture()
        self.recover(stop=1)
        baseline = self.path/'track03.bin.recovery-base'
        data = bytearray(baseline.read_bytes()); data[-1] ^= 1; baseline.write_bytes(data)
        previous = self.track.read_bytes()
        result = self.recover()
        self.assertEqual(result[0],'-1')
        self.assertEqual(result[4],'0')
        self.assertIn('baseline',result[6])
        self.assertEqual(self.track.read_bytes(),previous)

    def test_invalid_recovery_queue_never_modifies_track(self):
        previous = self.recovery_fixture()
        queue = self.path/'track03.bin.bad'
        queue.write_text(queue.read_text()+'3,999,45999,46149,2352\n')
        result = self.recover()
        self.assertEqual(result[0],'-1')
        self.assertEqual(result[4],'0')
        self.assertEqual(self.track.read_bytes(),previous)

    def thread(self, fault=0, recovery=0):
        return subprocess.check_output([str(self.exe),'thread',str(self.disc),str(self.path),
            str(fault),str(recovery)],text=True).strip().split('|')

    def test_first_pass_prompts_and_withholds_completion_until_recovered(self):
        result = self.thread(fault=2)
        folder = self.path/'fixture'
        self.assertEqual(result[:3],['1','3','OK'])
        self.assertFalse((folder/'rip.complete').exists())
        self.assertTrue((folder/'rip.first-pass').exists())
        self.assertEqual((folder/'track03.bin').stat().st_size,len(self.disc_data))
        # Start offers the saved queue again without automatically hammering it.
        self.assertEqual(self.thread(fault=2)[:3],['1','3','OK'])
        result = self.thread(recovery=1)
        self.assertEqual(result[0],'0')
        self.assertTrue((folder/'rip.complete').exists())
        self.assertEqual((folder/'track03.bin').read_bytes(),self.disc_data)

    def test_recovery_rejects_different_disc_before_touching_targets(self):
        self.thread(fault=2)
        folder = self.path/'fixture'
        before = (folder/'track03.bin').read_bytes()
        self.disc.write_bytes(make_sector(45150,99)+self.disc_data[2352:])
        result = self.thread(recovery=1)
        self.assertEqual(result[0],'0')
        self.assertFalse((folder/'rip.complete').exists())
        self.assertEqual((folder/'track03.bin').read_bytes(),before)

    def test_clean_first_pass_finishes_without_recovery_prompt(self):
        self.assertEqual(self.thread()[0],'0')
        self.assertTrue((self.path/'fixture'/'rip.complete').exists())

    def test_memory_fault_stops_first_pass_instead_of_becoming_disc_recovery(self):
        result = self.thread(fault=4)
        self.assertEqual(result[:3],['0','0','Memory changed during write'])
        self.assertFalse((self.path/'fixture'/'rip.complete').exists())
        self.assertFalse((self.path/'fixture'/'rip.first-pass').exists())

    def test_recovery_prompt_keeps_its_folder_when_disc_auto_name_changes(self):
        self.assertEqual(self.run_c('recovery-controls'),['ok'])

    def test_advanced_read_retries_silent_corruption(self):
        result = self.rip(advanced=1,fault=1)
        self.assertEqual(result[0],'0')
        self.assertGreater(int(result[1]),2)
        self.assertEqual(self.track.read_bytes(),self.disc_data)

    def test_normal_read_retries_silent_payload_corruption(self):
        result = self.rip(advanced=0, fault=1)
        self.assertEqual(result[0], '0')
        self.assertGreater(int(result[1]), 2)
        self.assertEqual(self.track.read_bytes(), self.disc_data)

    def test_write_buffer_mutation_stops_and_resume_cannot_approve_corrupted_data(self):
        result = self.rip(fault=4)
        self.assertEqual(result[0], '-1')
        self.assertEqual(result[2:4], ['0', '00000000'])
        self.assertNotEqual(self.track.read_bytes(), self.disc_data[:16*2352])
        rows = (self.path/'track03.bin.bad').read_text().splitlines()
        self.assertEqual(len(rows), 17)  # Conservatively flag the whole write.
        self.assertEqual(rows[1].split(',')[1:4], ['0', '45000', '45150'])
        resumed = self.rip()
        self.assertEqual(resumed[0], '0')
        self.assertNotEqual(int(resumed[3], 16), zlib.crc32(self.disc_data))
        self.assertEqual(self.run_c('verify', self.path, self.db, 1)[0], '8')
        self.run_c('verify', self.path, self.db, 0)
        self.rip(advanced=1)
        self.assertEqual(self.track.read_bytes(), self.disc_data)
        self.assertEqual(self.run_c('verify', self.path, self.db, 0)[0], '7')

    def test_post_copy_buffer_mutation_requires_scan_even_when_saved_bytes_are_good(self):
        result = self.rip(fault=5)
        self.assertEqual(result[0], '-1')
        self.assertEqual(result[2:4], ['0', '00000000'])
        self.assertEqual(self.track.read_bytes(), self.disc_data[:16*2352])
        self.assertTrue((self.path/'track03.bin.bad').exists())

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

    def scan_fault(self, fault):
        return self.run_c('verify', self.path, self.db, 0, fault)

    def test_readback_detects_transient_bad_bytes_without_repairing_or_replacing_crc(self):
        self.rip()
        journal = (self.path/'track03.bin.crc').read_bytes()
        result = self.scan_fault(1)
        self.assertEqual(result[:2], ['9','1'])
        self.assertEqual(int(result[2]), len(self.disc_data)+2*2352)
        self.assertEqual(self.track.read_bytes(), self.disc_data)
        self.assertEqual((self.path/'track03.bin.crc').read_bytes(), journal)
        report = (self.path/'verify.log').read_text()
        self.assertIn('readback_disagreements 1\n', report)
        self.assertIn('buffer_changes 0\n', report)
        self.assertIn('result STORAGE READ-BACK INCONSISTENT\n', report)
        samples = (self.path/'readback.bin').read_bytes()
        self.assertEqual(len(samples), 3*2352)
        expected = self.disc_data[15*2352:16*2352]
        self.assertNotEqual(samples[:2352], expected)
        self.assertEqual(samples[2352:], expected*2)
        damaged = bytearray(self.disc_data)
        damaged[15*2352:16*2352] = samples[:2352]
        self.assertIn(f'{zlib.crc32(damaged):08x} size_ok', report)
        self.assertIn('diff 2336 ', (self.path/'readback.log').read_text())
        self.assertTrue((self.path/'readback.unstable').exists())

    def test_readback_reports_repeatable_invalid_read_without_claiming_transience(self):
        self.rip()
        self.assertEqual(self.scan_fault(2)[:2], ['8','1'])
        report = (self.path/'verify.log').read_text()
        self.assertIn('readback_disagreements 0\n', report)
        self.assertEqual(self.track.read_bytes(), self.disc_data)
        samples = (self.path/'readback.bin').read_bytes()
        self.assertEqual(samples, samples[:2352]*3)
        self.assertNotEqual(samples[:2352], self.disc_data[15*2352:16*2352])

    def test_readback_detects_buffer_change_during_progress_even_when_crc_matches(self):
        self.rip()
        self.assertEqual(self.scan_fault(3)[0], '9')
        report = (self.path/'verify.log').read_text()
        self.assertIn('catalog_result FULL TRACK MATCH\n', report)
        self.assertIn('buffer_changes 1\n', report)
        self.assertIn('phase progress-and-yield', (self.path/'readback.log').read_text())
        self.assertEqual(self.track.read_bytes(), self.disc_data)

    def test_readback_detects_buffer_change_during_suspect_map_write(self):
        self.rip()
        damaged = bytearray(self.disc_data); damaged[100] ^= 1
        self.track.write_bytes(damaged)
        self.assertEqual(self.scan_fault(4)[0], '9')
        self.assertIn('phase diagnostics-and-map', (self.path/'readback.log').read_text())
        self.assertEqual(self.track.read_bytes(), damaged)

    def test_readback_reread_errors_and_cancellation_preserve_files_and_existing_guard(self):
        self.rip()
        damaged = bytearray(self.disc_data); damaged[100] ^= 1
        self.track.write_bytes(damaged)
        marker = self.path/'readback.unstable'; marker.write_text('previous disagreement\n')
        self.assertEqual(self.scan_fault(5)[0], '-1')
        self.assertIn('reread_error pass 1', (self.path/'readback.log').read_text())
        self.assertTrue(marker.exists())
        self.assertEqual(self.scan_fault(6)[0], '0')
        self.assertEqual(len((self.path/'readback.bin').read_bytes()), 2*2352)
        self.assertEqual(self.track.read_bytes(), damaged)
        self.assertTrue(marker.exists())

    def test_readback_samples_are_bounded_on_heavily_damaged_track(self):
        self.track.write_bytes(bytes(2352*64))
        (self.path/'rip.state').write_text('DreamShell GD Ripper state v1\ndisc_type 128\nuse_bin 1\ntracks 1\n3 45150 64 4 2352 track03.bin\n')
        (self.path/'rip.complete').write_text('Complete: 64 sectors\n')
        self.assertEqual(self.scan_fault(0)[:2], ['8','64'])
        report = (self.path/'verify.log').read_text()
        self.assertIn('diagnostic_sectors 32\n', report)
        self.assertIn('diagnostic_skipped 32\n', report)
        self.assertEqual(len((self.path/'readback.bin').read_bytes()), 32*3*2352)

    def test_consistent_full_storage_scan_clears_guard_but_stream_check_does_not(self):
        self.rip()
        marker = self.path/'readback.unstable'; marker.write_text('previous disagreement\n')
        self.assertEqual(self.run_c('verify', self.path, self.db, 1)[0], '7')
        self.assertTrue(marker.exists())
        self.assertEqual(self.scan_fault(0)[0], '7')
        self.assertFalse(marker.exists())

    def test_readback_guard_blocks_disc_repair_before_track_modification(self):
        self.thread()
        folder = self.path/'fixture'
        before = (folder/'track03.bin').read_bytes()
        (folder/'readback.unstable').write_text('previous disagreement\n')
        result = self.thread(recovery=1)
        self.assertEqual(result[2], 'Storage reads disagree')
        self.assertEqual((folder/'track03.bin').read_bytes(), before)

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

    def test_app_consumes_events_after_forwarding_once(self):
        self.assertEqual(self.run_c('input-once'), ['ok'])

    def test_browse_directories_pages_cancel_and_select_existing_dump(self):
        root = self.path/'mount'; root.mkdir()
        for name in ['00 dump', '01 empty'] + [f'{i:02} folder' for i in range(2,19)]:
            (root/name).mkdir()
        (root/'00 dump'/'rip.state').write_text('fixture')
        (root/'a file.bin').write_text('not a folder')
        self.assertEqual(self.run_c('folders', root), ['ok'])

    def test_stop_returns_and_focus_skips_disabled_buttons(self):
        self.assertEqual(self.run_c('controls'),['ok'])

if __name__ == '__main__':unittest.main()
