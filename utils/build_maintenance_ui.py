#!/usr/bin/env python3
"""Reproducible native SDL layouts for the five NeXT maintenance apps."""
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
            a.set('version', '3.0.1')
            a.set('icon', 'images/icon_small.png')
            dep = node(r, 'module', src='../../modules/bflash.klf')
            r.remove(dep); r.insert(0, dep)
        button(b, r, 'menu', 'Menu', 492, 32, 116, 32, f'export:{native}_Back()')
        surface(r, 'maintenance-panel', 576, 252, PANEL)
        p = node(b, 'panel', name='main-panel', x=32, y=88, width=576, height=252,
                 background='maintenance-panel')
        for i in range(6):
            button(p, r, f'row-{i}', '', 0, i*34, 576, 30, f'export:{native}_Row()')
        label(p, 'detail-0', '', 12, 208, 552, 18, 'tiny', MUTED)
        label(p, 'detail-1', '', 12, 229, 552, 18, 'tiny', MUTED)
        for i, caption in enumerate(actions):
            button(b, r, f'action-{i}', caption, 32+i*194, 398, 186 if i<2 else 188, 30,
                   f'export:{native}_Action()')
        for name, color, height in [('picker-bg','#14212E',188),('picker-row','#182838',28),
                                    ('picker-focus','#225765',28),('picker-select','#155B71',28)]:
            surface(r, name, 576 if height==188 else 556, height, color)
        p = node(b, 'panel', name='browser-panel', x=32, y=88, width=576, height=252,
                 background='maintenance-panel')
        label(p, 'picker-path', '/', 8, 0, 560, 24, 'small', ACCENT)
        for name, text, x, w, fn in [('up','Up',0,80,'Up'),('devices','Devices',88,116,'Devices'),
                                  ('choose-folder','Use folder (X)',212,192,'Folder'),
                                  ('picker-cancel','Cancel',412,164,'Back')]:
            button(p, r, name, text, x, 28, w, 30, f'export:{native}_{fn}()')
        node(p, 'filemanager', name='file-picker', path='/', x=0, y=64, width=576, height=188,
             background='picker-bg', item_font='small', item_font_color='#EFF5FC',
             item_normal='picker-row', item_highlight='picker-focus', item_pressed='picker-select',
             item_disabled='picker-row', item_selected_normal='picker-select',
             item_selected_highlight='picker-focus', item_selected_pressed='picker-select',
             item_selected_disabled='picker-row', onclick=f'export:{native}_Item()')
        label(b, 'status', 'Ready.', 32, 344, 576, 20, 'small', ACCENT)
        label(b, 'note', '', 32, 365, 576, 18, 'tiny', MUTED)
        surface(r, 'progress-bg', 576, 4, '#263749')
        surface(r, 'progress-fill', 576, 4, ACCENT)
        node(b, 'panel', x=32, y=388, width=576, height=4, background='progress-bg')
        node(b, 'panel', name='progress', x=32, y=388, width=576, height=4, background='progress-fill')
        label(b, 'controls', 'D-pad Select / Adjust     A Choose     B Back / Stop     START Menu',
              32, 432, 576, 16, 'tiny', MUTED)
        node(b, 'dialog', name='dialog', font='body', x=50, y=80, width=540, height=326,
             onconfirm=f'export:{native}_Confirm()', oncancel=f'export:{native}_Cancel()')
        save(a, folder)

if __name__ == '__main__': layouts()
