"""NAM captures for the Clone slots (src/nam/capture.h, docs/nam.md): the A2-Lite network of a .nam file packed
into the bytes a slot holds.

    from waveod_studio import nam
    data, info = nam.capture('amp.nam')          # bytes for Pedal.write_clone, and what they hold

A .nam is accepted when it is the A2-Lite network (a 3-channel WaveNet of the A2 shape) or a SlimmableContainer
holding one (the full A2's .nam files do: the 3-channel submodel is taken). The shape check follows
is_a2_shape() in NeuralAmpModelerCore (NAM/wavenet/a2_fast.cpp).

Packed order (A2_NUM_WEIGHTS floats), every per-tap matrix column-major (w[j*3+i] = in j -> out i):
  rechannel[3]
  per layer: conv[K][9], conv_b[3], mixin[3], l1x1[9], l1x1_b[3]
  head[16][3], head_b, head_scale
"""
import json
import os
import struct
import zlib

C = 3
KERNELS = [6] * 14 + [15, 15] + [6] * 7
DILATIONS = [1, 3, 7, 17, 41, 101, 239] * 2 + [1, 13] + [1, 3, 7, 17, 41, 101, 239]
HEAD_K = 16
NUM_WEIGHTS = C + sum(9 * k + 3 + 3 + 9 + 3 for k in KERNELS) + HEAD_K * C + 2     # 1871

MAGIC, VERSION, HEADER, NATIVE_RATE = 0x324D414E, 1, 0x88, 1                     # src/nam/capture.h
SLOT_BYTES = 0x2000                                                              # what a Clone slot keeps
NAME_MAX = 16                                                                    # 17 bytes with the zero


def is_a2_lite(cfg):
    if not isinstance(cfg, dict) or len(cfg.get('layers', [])) != 1 or cfg.get('head') is not None:
        return False
    if cfg.get('condition_dsp') is not None or cfg.get('in_channels', 1) != 1:
        return False
    la = cfg['layers'][0]
    ok = (la.get('input_size') == 1 and la.get('condition_size') == 1 and la.get('channels') == C
          and la.get('bottleneck') == C and la.get('kernel_sizes') == KERNELS and la.get('dilations') == DILATIONS)
    acts = la.get('activation', [])
    ok = ok and len(acts) == len(KERNELS) and all(
        isinstance(a, dict) and a.get('type') == 'LeakyReLU' and abs(a.get('negative_slope', 0) - 0.01) < 1e-7
        for a in acts)
    ok = ok and all(g == 'none' for g in (la.get('gating_mode') or ['none'])) and not la.get('gated', False)
    ok = ok and all(s is None for s in (la.get('secondary_activation') or [None]))
    ok = ok and not (la.get('head1x1') or {}).get('active', False)
    l1 = la.get('layer1x1') or {}
    ok = ok and l1.get('active', False) and l1.get('groups', 1) == 1
    h = la.get('head') or {}
    ok = ok and h.get('out_channels') == 1 and h.get('kernel_size') == HEAD_K and h.get('bias', False)
    ok = ok and h.get('head_dilation', 1) == 1
    for k in ('conv_pre_film', 'conv_post_film', 'input_mixin_pre_film', 'input_mixin_post_film',
              'activation_pre_film', 'activation_post_film', 'layer1x1_post_film', 'head1x1_post_film'):
        f = la.get(k)
        if (isinstance(f, dict) and f.get('active', False)) or f is True:
            ok = False
    ok = ok and la.get('groups_input', 1) == 1 and la.get('groups_input_mixin', 1) == 1
    ok = ok and la.get('slimmable') is None
    return bool(ok)


def find_lite(m):
    """the A2-Lite network (a model dict) inside a .nam document, or None"""
    if m.get('architecture') == 'WaveNet' and is_a2_lite(m.get('config')):
        return m
    if m.get('architecture') == 'SlimmableContainer':
        for sub in m.get('config', {}).get('submodels', []):
            r = find_lite(sub.get('model', {}))
            if r:
                return r
    return None


def pack(weights):
    """the .nam's weight list in the engine's order"""
    if len(weights) != NUM_WEIGHTS:
        raise ValueError('esperados %d pesos, o arquivo tem %d' % (NUM_WEIGHTS, len(weights)))
    it = iter(weights)
    out = [next(it) for _ in range(C)]                       # rechannel (1 -> C)
    for K in KERNELS:
        conv = [0.0] * (K * 9)
        for i in range(C):                                    # out
            for j in range(C):                                # in
                for k in range(K):
                    conv[k * 9 + j * C + i] = next(it)
        out += conv
        out += [next(it) for _ in range(C)]                   # conv bias
        out += [next(it) for _ in range(C)]                   # input mixin
        l1 = [0.0] * 9
        for i in range(C):
            for j in range(C):
                l1[j * C + i] = next(it)
        out += l1
        out += [next(it) for _ in range(C)]                   # layer1x1 bias
    head = [0.0] * (HEAD_K * C)
    for j in range(C):
        for k in range(HEAD_K):
            head[k * C + j] = next(it)
    out += head
    out.append(next(it))                                      # head bias
    out.append(next(it))                                      # head_scale
    return out


def load(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def capture(path, native_rate=False, m=None):
    """(slot bytes, info) for a .nam file; native_rate: a 48 kHz model runs at 44.1 kHz (no resampling)"""
    m = m if m is not None else load(path)
    lite = find_lite(m)
    if not lite:
        raise ValueError('%s: não tem a rede A2-Lite (WaveNet A2 de 3 canais)' % os.path.basename(path))
    w = pack(lite['weights'])
    meta = lite.get('metadata') or m.get('metadata') or {}
    loud = meta.get('loudness')
    fs = float(lite.get('sample_rate') or m.get('sample_rate') or 48000)
    wb = struct.pack('<%df' % len(w), *w)
    hdr = struct.pack('<IHHffII', MAGIC, VERSION, len(w), float(loud) if loud is not None else -18.0, fs,
                      zlib.crc32(wb), NATIVE_RATE if native_rate else 0).ljust(HEADER, b'\0')
    data = hdr + wb
    assert len(data) <= SLOT_BYTES - 48         # the original firmware's editor buffer takes 0x2000 - 48
    at48 = 47000 < fs < 49000 and not native_rate
    return data, dict(weights=len(w), loudness=loud, sample_rate=fs, bytes=len(data),
                      runs_at='48 kHz (reamostrado)' if at48 else '44,1 kHz')


def is_capture(data):
    """whether a slot's bytes are a capture of this format (header and CRC)"""
    if len(data) < HEADER + NUM_WEIGHTS * 4:
        return False
    magic, version, n = struct.unpack_from('<IHH', data)
    crc, = struct.unpack_from('<I', data, 0x10)
    return magic == MAGIC and version == VERSION and n == NUM_WEIGHTS and \
        zlib.crc32(data[HEADER:HEADER + NUM_WEIGHTS * 4]) == crc


def default_name(path):
    return os.path.splitext(os.path.basename(path))[0][:NAME_MAX]
