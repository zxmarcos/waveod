// An IR's magnitude response for drawing (Matribox Studio's irwav.response): a 4096-point FFT of the 1024 samples,
// averaged over 1/6 octave around each of `points` log-spaced frequencies.

const RATE = 44100;

function fft(re: Float64Array, im: Float64Array) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j |= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let size = 2; size <= n; size <<= 1) {
    const a = (-2 * Math.PI) / size, wr = Math.cos(a), wi = Math.sin(a);
    for (let s = 0; s < n; s += size) {
      let cr = 1, ci = 0;
      for (let k = s; k < s + size / 2; k++) {
        const h = k + size / 2;
        const vr = re[h] * cr - im[h] * ci, vi = re[h] * ci + im[h] * cr;
        re[h] = re[k] - vr; im[h] = im[k] - vi;
        re[k] += vr; im[k] += vi;
        [cr, ci] = [cr * wr - ci * wi, cr * wi + ci * wr];
      }
    }
  }
}

/** [frequency, dB][] from 20 Hz to 20 kHz */
export function response(samples: number[], points = 160, smooth = 1 / 6, nfft = 4096): [number, number][] {
  const re = new Float64Array(nfft), im = new Float64Array(nfft);
  for (let i = 0; i < Math.min(samples.length, nfft); i++) re[i] = samples[i];
  fft(re, im);
  const half = nfft / 2 + 1;
  const cum = new Float64Array(half + 1);
  for (let k = 0; k < half; k++) cum[k + 1] = cum[k] + re[k] * re[k] + im[k] * im[k];
  const df = RATE / nfft, out: [number, number][] = [];
  for (let i = 0; i < points; i++) {
    const f = 20 * Math.pow(1000, i / (points - 1));
    const k1 = Math.max(0, Math.floor((f * Math.pow(2, -smooth / 2)) / df));
    const k2 = Math.min(half - 1, Math.max(k1, Math.ceil((f * Math.pow(2, smooth / 2)) / df)));
    const m = (cum[k2 + 1] - cum[k1]) / (k2 - k1 + 1);
    out.push([f, m > 1e-20 ? 10 * Math.log10(m) : -200]);
  }
  return out;
}

/** the curve as an SVG path in a w x h box: 20 Hz .. 20 kHz across, `top` dB at the top, `range` dB down */
export function curvePath(r: [number, number][], w: number, h: number, top: number, range = 48): string {
  return r.map(([f, db], i) => {
    const x = (Math.log10(f / 20) / 3) * w;
    const y = Math.min(h, Math.max(0, ((top - db) / range) * h));
    return `${i ? 'L' : 'M'}${x.toFixed(1)},${y.toFixed(1)}`;
  }).join('');
}

/** where to put a curve's top: its loudest point, rounded up to 6 dB */
export const topOf = (r: [number, number][]) => Math.ceil(Math.max(...r.map(([, d]) => d)) / 6) * 6;
