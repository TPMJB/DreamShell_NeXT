#!/usr/bin/env python3
"""Preview real maintenance XML with labelled sample data; not console emulation."""
from pathlib import Path
from PIL import Image
from preview_utility_apps import Preview, RESAMPLE

SAMPLES = {
 'bios_flasher': [
  'Chip:  MX29LV160T', 'Visible bank:  2048 KiB  /  Programming supported',
  'BIOS image:  /sd/BIOS/dreamshell_boot.bin', 'Backup folder:  /sd/Backups/BIOS',
  'Bank selection:  Physical switch on your hardware', 'Detect chip again'],
 'region_changer': [
  'Region:  USA  [draft]', 'Language:  English', 'Broadcast:  NTSC', 'Black swirl:  Off',
  'Backup folder:  /sd/Backups/Flash', 'Advanced:  Backup / restore / clear other settings'],
 'speedtest': [
  'Test:  File write + verify', 'Test folder:  /ide/Benchmarks', 'Data per pass:  8 MiB',
  'Passes:  3  /  Buffer: 256 KiB', 'Report folder:  /sd/Reports', 'Results:  2 runs  /  A to view latest'],
 'memtest': [
  '[x]  AICA          2 MiB  /  PASS  (details: X)',
  '[x]  Video RAM     8 MiB  /  PASS  (details: X)',
  '[x]  System RAM    16 MiB  /  PASS  (details: X)', '--', '--', 'Dreamcast  /  Full  /  3 passes'],
 'network': [
  'Interface:  Realtek RTL8139', 'IPv4 address:  192.168.1.42', 'Subnet mask:  255.255.255.0',
  'Gateway:  192.168.1.1', 'FTP server:  Started  /  A to stop', 'HTTP server:  Stopped  /  A to start'],
}
DETAILS = {
 'bios_flasher': ['Exact full-bank images only. No partial offsets or cross-bank erases.',
  'Write: verified backup first, full read-back verification afterwards.',
  'Chip detected. Select an image to compare or write.', 'Macronix / ID 00C2 / 2048 KiB chip'],
 'region_changer': ['On console: Europe / English / PAL / normal swirl',
  'Edits console region, language, video standard and swirl. Applies after restart.',
  'Draft changes. Review and apply when ready.', ''],
 'speedtest': ['Last run: write 4.35 MiB/s / read 8.16 MiB/s',
  'Unique temporary file; reopen, verify every byte, then remove it.',
  'PASS: 3/3 passes / write 4.35 / read 8.16 MiB/s',
  'MiB/s measures IO calls plus close. Caches stay enabled; compare the same mode.'],
 'memtest': ['A toggles a region. Left / X shows its last subtests. Options sets the test mode.',
  'Screen / sound may pause. B stops between regions; wait for the current test.',
  'PASS: 9/9 region tests completed, 0 failures / errors.',
  'X shows subtests for the selected region. Save report keeps the full run history.'],
 'network': ['FTP: ftp://192.168.1.42:21  /  guest access',
  'Interface status does not establish Internet or DNS reachability.',
  'Interface is up. Address and service status refreshed.', ''],
}

def main(folder):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    images=[];overflows=[]
    for app, rows in SAMPLES.items():
        values={f'row-{i}-caption':text for i,text in enumerate(rows)}
        values.update(zip(('detail-0','detail-1','status','note'),DETAILS[app]))
        if app=='network': values['action-0-caption']='Disconnect Ethernet'
        preview=Preview(app,values,focus='action-0',hidden=('browser-panel',))
        images.append(preview.save(folder/(app+'.png')))
        overflows.extend(preview.overflows)
    values={'picker-path':'/sd/BIOS', 'file-picker':['[..]','[Backups]', 'dreamshell_boot.bin', 'original_bios.bin'],
            'status':'Choose a file and press A to select it.',
            'note':'B: parent folder / cancel at Devices. Y: Devices. START: cancel picker.'}
    preview=Preview('bios_flasher',values,hidden=('main-panel','action-0','action-1','action-2'))
    images.append(preview.save(folder/'file-picker.png'));overflows.extend(preview.overflows)
    values={f'row-{i}-caption':text for i,text in enumerate(SAMPLES['memtest'])}
    values.update({'row-1-caption':'[x] Video RAM  /  RUNNING',
        'row-2-caption':'[x] System RAM  16 MiB  /  Not tested',
        'detail-0':DETAILS['memtest'][0], 'detail-1':DETAILS['memtest'][1],
        'status':'Pass 1/1: testing Video RAM. Please wait...',
        'note':'The current memory test must finish before B can skip the remaining regions.'})
    preview=Preview('memtest',values,hidden=('browser-panel',))
    preview.save(folder/'memtest-running.png');overflows.extend(preview.overflows)
    # Simulate the outer 5% being lost on a TV. The full controller legend and
    # action buttons must survive, without resizing the native interface.
    preview.canvas.crop((32*2,24*2,608*2,456*2)).save(folder/'memtest-tv-crop.png')
    values={f'row-{i}-caption':text for i,text in enumerate(SAMPLES['region_changer'])}
    values.update({'row-0-caption':'Region: Unknown / A to reread',
        'row-1-caption':'Language: Unknown', 'row-2-caption':'Broadcast: Unknown',
        'row-3-caption':'Black swirl: Unknown',
        'detail-0':'Unknown factory fields: region FF / language FF / broadcast FF',
        'detail-1':DETAILS['region_changer'][1],
        'status':'Factory fields are unrecognized. Back up or restore a valid file.',
        'note':'A on an unknown field retries the read. Menu remains available.'})
    preview=Preview('region_changer',values,hidden=('browser-panel',))
    preview.save(folder/'region-unknown.png');overflows.extend(preview.overflows)
    # Six screenshots at native resolution. All values here are samples.
    sheet=Image.new('RGB',(1280,1440),'#080F23')
    for i,im in enumerate(images):sheet.paste(im.resize((640,480),RESAMPLE),((i%2)*640,(i//2)*480))
    sheet.save(folder/'maintenance-apps.png')
    if overflows: raise SystemExit(f'{len(overflows)} labels overflow')

if __name__=='__main__':
    import sys
    main(sys.argv[1] if len(sys.argv)>1 else '/tmp/maintenance-previews')
