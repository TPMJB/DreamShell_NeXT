#!/usr/bin/env python3
"""Build captioned K-UI launch cards from the real host layout previews.

No console is emulated. Existing sample UI renders are preserved; the Advanced
page is rendered from the production XML using preview_utility_apps. The output
is promotional documentation only and does not alter any runtime asset.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from preview_utility_apps import Preview

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'docs/launch/1.0/showcase'
BG, PANEL, TEXT, MUTED = '#080F23', '#111A32', '#EFF5FC', '#A7B9CB'
CYAN, PINK = '#5DE1ED', '#F07DDC'
SOURCE_COMMIT = '8bcc59cb3567f9b16a5d59a98a904cb20212c802'
FONT = ROOT / 'resources/fonts/ttf/arial_lite.ttf'


def font(size):
    return ImageFont.truetype(str(FONT), size)


def lines(draw, text, face, width):
    result, line = [], ''
    for word in text.split():
        candidate = (line + ' ' + word).strip()
        if draw.textlength(candidate, font=face) > width and line:
            result.append(line)
            line = word
        else:
            line = candidate
    return result + ([line] if line else [])


def write_wrapped(draw, text, xy, face, width, spacing, fill, max_lines):
    wrapped = lines(draw, text, face, width)
    if len(wrapped) > max_lines:
        raise ValueError(f'Caption exceeds its frame: {text}')
    x, y = xy
    for line in wrapped:
        draw.text((x, y), line, font=face, fill=fill)
        y += spacing
    return y


def source_image(entry):
    if entry['id'] == 'ripper-advanced':
        p = Preview('gd_ripper', {}, focus='recover-btn',
                    hidden=('main-page', 'destination-page', 'recovery-page'))
        # The native GUI inherits button bounds and centers its nested labels;
        # make those defaults explicit for the generic host XML renderer.
        for button in p.xml.findall('.//input'):
            for label in button.findall('label'):
                width, height = int(button.get('width')), int(button.get('height'))
                text_width = p.draw.textlength(label.get('text', ''), font=p.fonts[label.get('font', 'body')]) / 2
                inset = max(0, round((width - text_width) / 2))
                label.set('x', str(inset))
                label.set('y', '0')
                label.set('width', str(width - inset))
                label.set('height', str(height))
        p.render(p.xml.find('body'))
        return p.canvas.convert('RGB'), 'applications/gd_ripper/app.xml'
    path = ROOT / 'docs/screenshots/k-ui' / (entry['id'] + '.png')
    return Image.open(path).convert('RGB'), str(path.relative_to(ROOT))


def card(entry, number, count, output):
    canvas = Image.new('RGB', (1280, 1344), BG)
    d = ImageDraw.Draw(canvas)
    d.rectangle((0, 0, 1279, 5), fill=CYAN)
    d.rectangle((960, 0, 1279, 5), fill=PINK)
    d.text((32, 18), 'K-UI 1.0  /  KATANA USER INTERFACE', font=font(25), fill=CYAN)
    title_face = font(45)
    if d.textlength(entry['title'], font=title_face) > 980:
        title_face = font(38)
    d.text((32, 55), entry['title'], font=title_face, fill=TEXT)
    d.text((1124, 61), f'{number:02d} / {count:02d}', font=font(27), fill=PINK)
    d.text((32, 107), 'HOST LAYOUT PREVIEW / SAMPLE DATA', font=font(19), fill=MUTED)
    screen, source = source_image(entry)
    screen = screen.resize((1280, 960), Image.Resampling.NEAREST)
    canvas.paste(screen, (0, 144))
    d.line((0, 143, 1279, 143), fill='#263749', width=2)
    d.rectangle((0, 1104, 1279, 1343), fill=PANEL)
    d.line((32, 1116, 1248, 1116), fill=CYAN, width=2)
    d.text((32, 1133), entry['headline'], font=font(34), fill=CYAN)
    write_wrapped(d, entry['caption'], (32, 1180), font(27), 1216, 33, TEXT, 3)
    d.text((32, 1310), 'by TPMJB  /  github.com/TPMJB', font=font(20), fill=MUTED)
    d.text((858, 1310), 'Illustrative values; not a hardware capture', font=font(19), fill=MUTED)
    filename = f'{number:02d}-{entry["id"]}.jpg'
    target = output / filename
    canvas.save(target, 'JPEG', quality=88, subsampling=0, optimize=True)
    return canvas, dict(entry, file=filename, dimensions=list(canvas.size),
                        source=source, source_commit=SOURCE_COMMIT,
                        console_capture=False,
                        sha256=hashlib.sha256(target.read_bytes()).hexdigest())


def overview(entries, output):
    selected = ['launcher', 'gd-ripper', 'games', 'iso-loader', 'file-manager', 'vmu-manager']
    canvas = Image.new('RGB', (1440, 1480), BG)
    d = ImageDraw.Draw(canvas)
    d.rectangle((0, 0, 1439, 5), fill=CYAN)
    d.text((36, 24), 'K-UI 1.0', font=font(60), fill=TEXT)
    d.text((380, 43), 'A closer look at your Dreamcast tools', font=font(32), fill=CYAN)
    d.text((36, 104), 'HOST LAYOUT PREVIEWS / SAMPLE DATA / by TPMJB', font=font(23), fill=MUTED)
    for index, name in enumerate(selected):
        entry = next(e for e in entries if e['id'] == name)
        x = 36 + (index % 2) * 702
        y = 164 + (index // 2) * 418
        screenshot, _ = source_image(entry)
        canvas.paste(screenshot.resize((480, 360), Image.Resampling.LANCZOS), (x, y + 30))
        # Preserve a 4:3 app image and put the caption alongside it.
        d.text((x, y), entry['title'], font=font(27), fill=TEXT)
        write_wrapped(d, entry['headline'], (x + 496, y + 68), font(26), 169, 33, CYAN, 5)
    d.text((36, 1430), 'Ripping / recovery / games / files / saves / system utilities', font=font(25), fill=MUTED)
    canvas.save(output / '00-k-ui-overview.jpg', 'JPEG', quality=88, subsampling=0, optimize=True)
    return canvas


def documents(manifest, output):
    preface = ('These images show host-rendered K-UI layouts with sample data, not console or emulator captures. '
               'Game lists, progress, benchmark values, pass/fail statuses and hardware identities are illustrative. '
               'The boot recovery preview uses a substitute host font. Promotional frames and captions sit outside the app UI. '
               'K-UI is by TPMJB, built on SWAT\'s DreamShell and the credited upstream projects.')
    guide = ['# K-UI 1.0 — tool-by-tool showcase', '', preface, '',
             'This repeats the individual-app format of the [NeXT 0.9 gallery](https://github.com/TPMJB/DreamShell_NeXT/blob/74f82fc98918249f3a08aec56e43ee90d8e340bb/docs/screenshots/README.md), '
             'with the K-UI identity and current feature explanations.', '',
             '![K-UI overview](00-k-ui-overview.jpg)', '',
             '## Suggested Reddit gallery', '',
             'Use the square cover from the parent launch kit, then Launcher, GD Ripper, '
             'Advanced/recovery, Games, ISO Loader, VMU Manager and File Manager. '
             'Attach a real-console clip separately. Use the remaining utility cards in the forum post or a follow-up.', '',
             '## Individual tools', '']
    captions = ['# K-UI 1.0 — image captions and significance', '', preface, '']
    html_cards = []
    for e in manifest:
        for target in (guide, captions):
            target += [f'## {e["title"]}', '', f'**{e["headline"]}**', '',
                       f'![{e["title"]} layout preview]({e["file"]})', '',
                       e['caption'], '', '**Why it matters:** ' + e['significance'], '']
        html_cards.append('<article><img loading="lazy" src="' + html.escape(e['file']) +
                          '" alt="' + html.escape(e['title']) + ' layout preview"><div><h2>' +
                          html.escape(e['title']) + '</h2><p>' + html.escape(e['significance']) + '</p></div></article>')
    tail = ['## Release note to retain when sharing', '',
            'One intermittent GD-ROM dumping crash, reported with MDK2, remains unresolved. '
            'Recovery is not guaranteed; game/device compatibility varies. A catalog CRC match applies '
            'to the represented track files and does not establish subchannel/lead-in capture.', '',
            'Post after the [versioned release](https://github.com/TPMJB/DreamShell_NeXT/releases/tag/k-ui-1.0) '
            'and its ZIP are publicly available. Keep the preview labels and upstream credit. '
            'The [Reddit](../reddit-post.md) and [forum](../forum-post.bbcode.txt) drafts are in the parent launch kit.', '']
    (output / 'README.md').write_text('\n'.join(guide + tail))
    (output / 'captions.md').write_text('\n'.join(captions + tail))
    (output / 'image-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    document = '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>K-UI 1.0 — Tool showcase</title><style>body{margin:0;background:#080f23;color:#eff5fc;font:17px/1.55 system-ui}header,main,footer{max-width:1280px;margin:auto;padding:28px}h1{font-size:48px;margin:0;color:#5de1ed}header p{max-width:950px;color:#a7b9cb}.gallery{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:26px}article{background:#111a32;border:1px solid #263749;border-radius:8px;overflow:hidden}img{display:block;width:100%;height:auto}article div{padding:8px 24px 22px}h2{color:#5de1ed}a{color:#5de1ed}footer{color:#a7b9cb}</style><header><h1>K-UI 1.0</h1><p>Katana User Interface / by TPMJB</p><p>' + html.escape(preface) + '</p></header><main class="gallery">' + ''.join(html_cards) + '</main><footer><p>Known issue: an intermittent MDK2 dumping crash remains unresolved. Recovery and game/device compatibility are not guaranteed.</p><a href="https://github.com/TPMJB/DreamShell_NeXT">Source, release and guides</a></footer></html>'
    (output / 'index.html').write_text(document)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=DEST)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    entries = json.loads((DEST / 'copy.json').read_text())
    cards, manifest = [], []
    for number, entry in enumerate(entries, 1):
        canvas, metadata = card(entry, number, len(entries), args.output)
        cards.append(canvas)
        manifest.append(metadata)
    overview(entries, args.output)
    documents(manifest, args.output)
    print(f'Created {len(cards)} individual tool images, one overview, captions and HTML gallery in {args.output}')


if __name__ == '__main__':
    main()
