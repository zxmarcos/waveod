"""Backups of the pedal's presets: a .wvbak file is a zip with manifest.json and one waveod-format preset per
place (presets/NNN.wvp). The original's presets are saved too (the pedal converts them), so a restore brings
back every place as it was; empty places are left out and stay as they are on restore.

With firmware of protocol 3 the pedal's global control table goes too (global_controls.wvc, the table as its
chunk 11, manifest "global_controls"); a full restore writes it back, a restore of some presets leaves it."""
import datetime
import json
import zipfile

from waveod_studio import controls, wvpreset
from waveod_studio.i18n import tr
from waveod_studio.protocol import PLACES

FORMAT = 'waveod-backup'
GLOBAL_FILE = 'global_controls.wvc'


def save(pedal, path, progress=None, cancel=None):
    info = pedal.info()
    listing = pedal.presets()
    keep = [e for e in listing if e['place'] == 0 or (e['place'] == 1 and e['origin'] != 2)]
    manifest = dict(format=FORMAT, version=1, created=datetime.datetime.now().isoformat(timespec='seconds'),
                    firmware=info['version'], presets=[])
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        if info['protocol'] >= 3:
            glob = pedal.global_controls()
            z.writestr(GLOBAL_FILE, glob.chunk())
            manifest['global_controls'] = dict(file=GLOBAL_FILE, links=len(glob.links))
        for k, e in enumerate(keep):
            if cancel and cancel():
                raise RuntimeError(tr('py.cancelled'))
            origin, data = pedal.get(e['index'])
            name = 'presets/%03d.wvp' % e['index']
            z.writestr(name, data)
            manifest['presets'].append(dict(index=e['index'], label=wvpreset.preset_label(e['index']),
                                            name=e['name'], place=PLACES.get(e['place']), file=name))
            if progress:
                progress(k + 1, len(keep))
        z.writestr('manifest.json', json.dumps(manifest, indent=1, ensure_ascii=False))
    return manifest


def read(path):
    """(manifest, {index: bytes}); every preset checked to be waveod's format"""
    with zipfile.ZipFile(path) as z:
        manifest = json.loads(z.read('manifest.json'))
        if manifest.get('format') != FORMAT:
            raise ValueError(tr('py.backup_not_waveod', path=path))
        data = {}
        for e in manifest['presets']:
            b = z.read(e['file'])
            wvpreset.Preset.decode(b)
            data[int(e['index'])] = b
    return manifest, data


def read_global(path):
    """the global control table of a backup (controls.Table), or None when it has none"""
    with zipfile.ZipFile(path) as z:
        manifest = json.loads(z.read('manifest.json'))
        g = manifest.get('global_controls')
        if not g:
            return None
        t = controls.Table.from_chunk(z.read(g['file']))
    if t is None:
        raise ValueError(tr('py.backup_global_bad', path=path))
    return t


def restore(pedal, path, progress=None, cancel=None, only=None, global_controls=None):
    """the presets (only: those indexes) and, on a full restore (global_controls None) or when asked, the global
    control table (when the backup has one and the pedal's firmware takes it, protocol 3); how many presets"""
    manifest, data = read(path)
    glob = read_global(path)
    if global_controls is None:
        global_controls = only is None
    if glob is not None and global_controls and pedal.info()['protocol'] >= 3:   # older firmware: presets only
        pedal.set_global_controls(glob)
    items = sorted(i for i in data if only is None or i in only)
    for k, i in enumerate(items):
        if cancel and cancel():
            raise RuntimeError(tr('py.cancelled'))
        pedal.put(i, data[i])
        if progress:
            progress(k + 1, len(items))
    return len(items)
