"""Package current guides once, with links that work outside the source tree."""
import posixpath
import re
from urllib.parse import quote, unquote, urlsplit


GUIDES = {
    'README-FIRST.md': 'RELEASE-NOTES.md',
    'host-tools/README.md': 'utils/README.gd-verify.md',
    'exfat-guide.md': 'utils/README.exfat.md',
    'input-ui-guide.md': 'utils/README.input-ui.md',
    'readback-guide.md': 'utils/README.readback-diagnostic.md',
    'utility-apps-guide.md': 'utils/README.utility-apps.md',
    'iso-loader-guide.md': 'utils/README.iso-loader-next.md',
    'vmu-manager-guide.md': 'utils/README.vmu-manager.md',
    'bootloader-guide.md': 'utils/README.boot-branding.md',
    'menu-music-guide.md': 'utils/README.menu-music.md',
    'launcher-guide.md': 'utils/README.launcher.md',
    'installation-guide.md': 'docs/installation.md',
    'compatibility.md': 'docs/compatibility.md',
    'licensing.md': 'docs/licensing.md',
    'attribution.md': 'docs/attribution.md',
    'building.md': 'docs/building.md',
    'timestalkers-recovery.md': 'docs/timestalkers-recovery.md',
    'upstream-review.md': 'docs/upstream-review.md',
    'CONTRIBUTING.md': 'CONTRIBUTING.md',
}
SOURCE_OUTPUTS = {source: output for output, source in GUIDES.items()}
SOURCE_OUTPUTS.update({'LICENSE': 'DS/doc/LICENSE', 'NOTICE': 'DS/doc/NOTICE'})
LINK = re.compile(r'(?P<prefix>!?\[[^\]\n]*\]\()(?P<url>[^\s)]+)(?P<suffix>\))')


def render_guide(root, output, commit, public=False):
    source = GUIDES[output]
    text = (root / source).read_text()

    def link(match):
        url = match['url']
        parts = urlsplit(url)
        if parts.scheme or parts.netloc or not parts.path:
            return match.group()
        target = posixpath.normpath(posixpath.join(posixpath.dirname(source), unquote(parts.path)))
        if target.startswith('../') or target.startswith('/') or not (root / target).exists():
            raise ValueError(f'Broken source documentation link in {source}: {url}')
        if match['prefix'].startswith('!'):
            rendered = f'https://raw.githubusercontent.com/TPMJB/DreamShell_NeXT/{commit}/{quote(target, safe="/")}'
        elif target in SOURCE_OUTPUTS and not public:
            rendered = posixpath.relpath(SOURCE_OUTPUTS[target], posixpath.dirname(output) or '.')
        else:
            kind = 'tree' if (root / target).is_dir() else 'blob'
            rendered = f'https://github.com/TPMJB/DreamShell_NeXT/{kind}/{commit}/{quote(target, safe="/")}'
        if parts.query:
            rendered += '?' + parts.query
        if parts.fragment:
            rendered += '#' + parts.fragment
        return match['prefix'] + rendered + match['suffix']

    return LINK.sub(link, text).encode('utf-8')
