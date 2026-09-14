#!/usr/bin/env python3
"""Reproducible native SDL layouts for the four NeXT maintenance apps."""
from build_utility_ui import app, node, label, surface, button, save, PANEL, MUTED, ACCENT

APPS = {
    'bios_flasher': ('BIOS Flasher', 'BiosFlasher', 'Inspect. Back up. Write. Verify.',
                     ['Back up BIOS', 'Compare image', 'Write BIOS']),
    'region_changer': ('Region Changer', 'RegionChanger', 'Your console. Your region.',
                       ['Back up', 'Restore file', 'Apply changes']),
    'speedtest': ('Speedtest', 'Speedtest', 'Measure storage. Verify the data.',
                  ['Run test', 'Save report', 'Clear results']),
    'memtest': ('Memtest', 'Memtest', 'Memory diagnostics with results you can keep.',
                ['Run test', 'Save report', 'Options / Results']),
    'network': ('Network', 'NetworkApp', 'Connections, services and startup preferences.',
                ['Connect Ethernet', 'Refresh status', 'Options / Status']),
}

def layouts():
    for folder, (title, native, subtitle, actions) in APPS.items():
        a, r, b = app(folder, title, 'DreamShell NeXT  /  ' + subtitle, native)
        if folder == 'bios_flasher':
            a.set('version', '3.0.0')
            dep = node(r, 'module', src='../../modules/bflash.klf')
            r.remove(dep); r.insert(0, dep)
        button(b, r, 'menu', 'Menu', 500, 20, 116, 32, f'export:{native}_Back()')
        surface(r, 'maintenance-panel', 592, 274, PANEL)
        p = node(b, 'panel', name='main-panel', x=24, y=82, width=592, height=274,
                 background='maintenance-panel')
        for i in range(6):
            button(p, r, f'row-{i}', '', 0, i*38, 592, 34, f'export:{native}_Row()')
        label(p, 'detail-0', '', 12, 232, 568, 18, 'tiny', MUTED)
        label(p, 'detail-1', '', 12, 251, 568, 18, 'tiny', MUTED)
        for i, caption in enumerate(actions):
            button(b, r, f'action-{i}', caption, 24+i*200, 430, 192, 32,
                   f'export:{native}_Action()')
        for name, color, height in [('picker-bg','#14212E',204),('picker-row','#182838',28),
                                    ('picker-focus','#225765',28),('picker-select','#155B71',28)]:
            surface(r, name, 592 if height==204 else 572, height, color)
        p = node(b, 'panel', name='browser-panel', x=24, y=82, width=592, height=274,
                 background='maintenance-panel')
        label(p, 'picker-path', '/', 8, 0, 576, 24, 'small', ACCENT)
        for name, text, x, w, fn in [('up','Up',0,80,'Up'),('devices','Devices',88,116,'Devices'),
                                  ('choose-folder','Use folder (X)',212,192,'Folder'),
                                  ('picker-cancel','Cancel',412,180,'Back')]:
            button(p, r, name, text, x, 30, w, 32, f'export:{native}_{fn}()')
        node(p, 'filemanager', name='file-picker', path='/', x=0, y=70, width=592, height=204,
             background='picker-bg', item_font='small', item_font_color='#EFF5FC',
             item_normal='picker-row', item_highlight='picker-focus', item_pressed='picker-select',
             item_disabled='picker-row', item_selected_normal='picker-select',
             item_selected_highlight='picker-focus', item_selected_pressed='picker-select',
             item_selected_disabled='picker-row', onclick=f'export:{native}_Item()')
        label(b, 'status', 'Ready.', 24, 365, 592, 22, 'small', ACCENT)
        label(b, 'note', '', 24, 390, 592, 18, 'tiny', MUTED)
        surface(r, 'progress-bg', 592, 4, '#263749')
        surface(r, 'progress-fill', 592, 4, ACCENT)
        node(b, 'panel', x=24, y=414, width=592, height=4, background='progress-bg')
        node(b, 'panel', name='progress', x=24, y=414, width=592, height=4, background='progress-fill')
        label(b, 'controls', 'D-pad Select / Adjust     A Choose     B Back / Stop     START Menu',
              24, 463, 592, 16, 'tiny', MUTED)
        node(b, 'dialog', name='dialog', font='body', x=50, y=80, width=540, height=326,
             onconfirm=f'export:{native}_Confirm()', oncancel=f'export:{native}_Cancel()')
        save(a, folder)

if __name__ == '__main__': layouts()
