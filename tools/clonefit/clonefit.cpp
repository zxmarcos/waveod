// clonefit: a .nam -> a Clone capture of the original's own format (docs/clone_fit.md).
//
//   clonefit MODEL.nam DI.wav|builtin OUT.clo [--target TARGET.wav] [--target-out T.wav] [--di-out D.wav]
//            [--slim S]
//
// The original's PC editor turns a .nam into a Clone capture in two steps, and so does this:
//  1. the model renders a reference DI signal (70 s at 44.1 kHz, resampled to the model's rate first), its output
//     x 0.31, kept at 16 bits (the editor writes it to a 16-bit WAV and reads it back);
//  2. a Wiener-Hammerstein fit of the pedal's Clone model to the DI / output pair (the editor's HTKPA::startClone,
//     here from its decompile: research/NOTES.md "Clone"): IIR 1 the identity, IIR 2 a 40 Hz high pass, the
//     saturation from the peaks of the first 5 s, the two IRs by iterAmpCoeff's passes and a last correction of IR 2.
// The model runs on NeuralAmpModelerCore (third_party/nam): any architecture it loads (WaveNet A1 of any size,
// A2 / SlimmableContainer, LSTM, ConvNet, Linear), any sample rate. --target skips step 1 (an output already
// rendered, e.g. the editor's own, for comparing the fits).
//
// Output: the capture, 0x2288 bytes ("VTSI", CRC-16 Modbus); the pedal keeps its first 0x2000.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <unsupported/Eigen/FFT>

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

using std::vector;
typedef vector<float> fvec;

static const float FLT_EPS = 1.1920928955078125e-07f;
static const float FMAX = std::numeric_limits<float>::max();

// ------------------------------------------------------------------------------------------------ WAV files

struct Wav {
    double rate = 0;
    fvec x;                                             // channel 0
};

static bool wav_read(const char *path, Wav &w)
{
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::vector<uint8_t> d;
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 12 || std::memcmp(&d[0], "RIFF", 4) || std::memcmp(&d[8], "WAVE", 4))
        return false;
    int fmt = 0, ch = 0, bits = 0;
    size_t at = 12;
    while (at + 8 <= d.size()) {
        uint32_t len;
        std::memcpy(&len, &d[at + 4], 4);
        const uint8_t *p = &d[at + 8];
        if (!std::memcmp(&d[at], "fmt ", 4)) {
            uint16_t f16, c16, b16;
            uint32_t sr;
            std::memcpy(&f16, p, 2);
            std::memcpy(&c16, p + 2, 2);
            std::memcpy(&sr, p + 4, 4);
            std::memcpy(&b16, p + 14, 2);
            fmt = f16 == 0xFFFE ? (p[24] | p[25] << 8) : f16;         // WAVE_FORMAT_EXTENSIBLE: the sub format
            ch = c16;
            w.rate = sr;
            bits = b16;
        } else if (!std::memcmp(&d[at], "data", 4) && ch > 0) {
            size_t bps = (size_t)bits / 8, frames = std::min<size_t>(len, d.size() - at - 8) / (bps * ch);
            w.x.resize(frames);
            for (size_t i = 0; i < frames; i++) {
                const uint8_t *s = p + i * bps * ch;
                float v = 0;
                if (fmt == 3 && bits == 32)
                    std::memcpy(&v, s, 4);
                else if (bits == 16)
                    v = (int16_t)(s[0] | s[1] << 8) / 32768.0f;
                else if (bits == 24)
                    v = (float)((int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8) /
                        8388608.0f;
                else if (bits == 32) {
                    int32_t q;
                    std::memcpy(&q, s, 4);
                    v = (float)((double)q / 2147483648.0);
                }
                w.x[i] = v;
            }
            return true;
        }
        at += 8 + len + (len & 1);
    }
    return false;
}

static void wav_write16(const char *path, const fvec &x, int rate)
{
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return;
    uint32_t data = (uint32_t)x.size() * 2, riff = 36 + data, sr = (uint32_t)rate, br = sr * 2, fmtlen = 16;
    uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtlen, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&ch, 2, 1, f);
    std::fwrite(&sr, 4, 1, f);
    std::fwrite(&br, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data, 4, 1, f);
    for (float v : x) {
        int q = (int)std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f);
        int16_t s = (int16_t)q;
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

// ------------------------------------------------------------------------------------------------ resampling

static double bessel_i0(double v)
{
    double s = 1, t = 1;
    for (int k = 1; k < 40; k++) {
        t *= (v / (2 * k)) * (v / (2 * k));
        s += t;
    }
    return s;
}

// windowed-sinc (Kaiser) resampler, zero phase: y[m] = x(m * from / to), the length scaled by the rates. Integer
// rates: a polyphase table (to / gcd phases); otherwise the kernel computed per output sample.
static fvec resample(const fvec &x, double from, double to)
{
    if (from == to)
        return x;
    const int half = 64;                                // zero crossings each side, at the lower rate
    const double beta = 9.0, ratio = to / from, cut = std::min(1.0, ratio) * 0.97, span = half / cut;
    const double i0b = bessel_i0(beta);
    auto kernel = [&](double d) {                       // d = t - k (input samples)
        double u = d * cut, r = d / span;
        if (std::fabs(r) >= 1)
            return 0.0;
        double sinc = u == 0 ? 1.0 : std::sin(M_PI * u) / (M_PI * u);
        return sinc * bessel_i0(beta * std::sqrt(1 - r * r)) / i0b * cut;
    };
    const size_t n = (size_t)std::llround((double)x.size() * ratio);
    fvec y(n);
    const long wide = (long)std::ceil(span) + 1;
    long ifrom = (long)from, ito = (long)to;
    if ((double)ifrom == from && (double)ito == to && ifrom > 0 && ito > 0) {
        long g = ifrom, h = ito;
        while (h) {
            long t = g % h;
            g = h;
            h = t;
        }
        const long P = ito / g, Q = ifrom / g;          // y[m] = x(m Q / P): P phases
        const int taps = (int)(2 * wide + 1);
        vector<double> tab((size_t)P * taps);
        for (long p = 0; p < P; p++)
            for (int j = 0; j < taps; j++)
                tab[(size_t)p * taps + j] = kernel((double)p / P + (double)(wide - j));
        for (size_t m = 0; m < n; m++) {
            long num = (long)(m % P) * Q, base = (long)(m / P) * Q + num / P, p = num % P;
            const double *k = &tab[(size_t)p * taps];
            double acc = 0;
            for (int j = 0; j < taps; j++) {
                long idx = base - wide + j;
                if (idx >= 0 && idx < (long)x.size())
                    acc += x[(size_t)idx] * k[j];
            }
            y[m] = (float)acc;
        }
        return y;
    }
    for (size_t m = 0; m < n; m++) {
        double t = (double)m / ratio;
        long lo = (long)std::ceil(t - span), hi = (long)std::floor(t + span);
        double acc = 0;
        for (long k = lo; k <= hi; k++)
            if (k >= 0 && k < (long)x.size())
                acc += x[(size_t)k] * kernel(t - k);
        y[m] = (float)acc;
    }
    return y;
}

// ------------------------------------------------------------------------------------------------ the DI

// a reference DI with the editor's sections (70 s at 44.1 kHz): 0-5 s a 300 Hz sine rising from -60 dBFS, 30 dB/s
// for 1 s, then 7.5 dB/s to full scale (the static curve); 6 s a click (the latency) then white noise at -62 dBFS rms to 21 s (the small-signal
// response); 23-28 s an exponential sweep 20 Hz - 10 kHz at -5 dBFS (the loud response); 30-50 s pink noise rising
// from -28.5 to -24.7 dBFS rms, a 1 s loop with a crest factor of 3 as the editor's (a gaussian or a longer loop
// fitted worse); 50-70 s guitar phrases (Karplus-Strong strings, single notes and chords) at -16.5 dBFS rms (the
// music the last IR 2 correction averages). The pink noise and the guitar get the long-term 1/3-octave shape of the
// editor's own DI (its levels only: no sample of it is used).
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return (uint32_t)(s >> 11);
    }
    double uni() { return (next() & 0xFFFFFF) / 16777216.0; }
    double gauss()
    {
        double u = std::max(uni(), 1e-12), v = uni();
        return std::sqrt(-2.0 * std::log(u)) * std::cos(2 * M_PI * v);
    }
};

static void pluck(vector<double> &out, size_t at, double hz, double amp, double dur, double bright, Rng &r)
{
    const double fs = 44100.0;
    double period = fs / hz;
    int L = (int)period;
    double frac = period - L;
    vector<double> line(L + 2);
    double lp = 0;
    for (auto &v : line) {                              // a filtered noise burst: the pick
        lp += (r.uni() * 2 - 1 - lp) * bright;
        v = lp;
    }
    double decay = std::pow(0.001, 1.0 / (dur * hz));   // -60 dB over dur
    size_t n = (size_t)(dur * fs);
    int pos = 0;
    double prev = 0, ap = 0, apc = (1 - frac) / (1 + frac);
    for (size_t i = 0; i < n && at + i < out.size(); i++) {
        double x = line[pos];
        int nxt = (pos + 1) % (L + 1);
        double avg = 0.5 * (x + line[nxt]) * decay;     // the string's loss
        double y = apc * avg + prev - apc * ap;         // all-pass: the fractional length
        prev = avg;
        ap = y;
        line[pos] = y;
        pos = nxt;
        double env = i < 64 ? i / 64.0 : 1.0;
        out[at + i] += amp * x * env;
    }
}

static fvec make_di()
{
    const double fs = 44100.0;
    const size_t N = (size_t)(70 * fs);
    vector<double> d(N, 0.0);
    for (size_t i = 0; i < (size_t)(5 * fs); i++) {      // -60 dB + 30 dB/s to 1 s, then 7.5 dB/s to 0 dB at 5 s
        double t = i / fs, db = t < 1.0 ? -60.0 + 30.0 * t : -30.0 + 7.5 * (t - 1.0);
        d[i] = std::pow(10.0, db / 20.0) * std::sin(2 * M_PI * 300.0 * t);
    }
    d[(size_t)(6 * fs)] = 1.0;
    Rng r(0x5EEDC10E);
    for (size_t i = (size_t)(6 * fs) + 1; i < (size_t)(21 * fs); i++)
        d[i] = 0.0008 * r.gauss();
    {
        const double f0 = 20.0, f1 = 10000.0, T = 5.0, K = std::log(f1 / f0);
        for (size_t i = 0; i < (size_t)(T * fs); i++) {
            double t = i / fs, ph = 2 * M_PI * f0 * T / K * (std::exp(t * K / T) - 1.0);
            double env = std::min(1.0, std::min(t, T - t) / 0.01);
            d[(size_t)(23 * fs) + i] = 0.5623 * env * std::sin(ph);
        }
    }
    // 1/3-octave band centres 20 Hz .. 16 kHz and the editor's DI's long-term levels there (dB re the section's
    // total): 30-50 s pink noise (flat bands), 50-70 s guitar playing
    static const double PINK[30] = { -19.4, -16.3, -13.7, -15.0, -15.3, -14.4, -14.0, -14.2, -15.0, -15.0, -14.5,
        -13.9, -14.3, -15.0, -15.0, -14.5, -13.8, -14.5, -15.0, -15.1, -14.3, -13.8, -14.6, -15.1, -15.0, -14.3,
        -13.8, -14.7, -15.1, -19.0 };
    static const double GUITAR[30] = { -39.3, -37.4, -36.2, -34.8, -34.4, -33.1, -16.0, -13.5, -13.5, -11.8, -10.5,
        -8.6, -8.0, -10.1, -11.3, -11.3, -12.8, -14.4, -15.6, -16.2, -17.7, -18.0, -20.7, -25.2, -31.5, -36.6,
        -41.7, -47.7, -52.8, -58.7 };
    // a section's long-term spectrum brought to a band shape (zero phase: the bins scaled, the gain interpolated in
    // dB between the band centres), its rms then a ramp from rms0 to rms1
    auto shape = [&](vector<double> &g, double t0, double t1, const double *want, double rms0, double rms1) {
        size_t a = (size_t)(t0 * fs), n = (size_t)((t1 - t0) * fs);
        vector<double> s(g.begin() + a, g.begin() + a + n);
        Eigen::FFT<double> fft;
        vector<std::complex<double>> S;
        fft.fwd(S, s);
        const size_t nb = n / 2 + 1;
        double cent[30], have[30], tot = 0;
        for (int k = 0; k < 30; k++) {
            cent[k] = 1000.0 * std::pow(2.0, (k - 17) / 3.0);
            double lo = cent[k] / std::pow(2.0, 1 / 6.0), hi = cent[k] * std::pow(2.0, 1 / 6.0), e = 0;
            for (size_t i = (size_t)std::ceil(lo * n / fs); i < nb && i * fs / n < hi; i++)
                e += std::norm(S[i]);
            have[k] = e;
            tot += e;
        }
        double gdb[30];
        for (int k = 0; k < 30; k++)
            gdb[k] = want[k] - 10.0 * std::log10(std::max(have[k], 1e-30) / tot);
        for (size_t i = 0; i < nb; i++) {
            double f = (double)i * fs / n, db;
            if (f <= cent[0])
                db = gdb[0];
            else if (f >= cent[29])
                db = gdb[29];
            else {
                double p = 3.0 * std::log2(f / 1000.0) + 17.0;
                int k = (int)p;
                db = gdb[k] + (gdb[k + 1] - gdb[k]) * (p - k);
            }
            double gain = std::pow(10.0, std::min(db, 60.0) / 20.0);
            S[i] *= gain;
            if (i && i < n - i)
                S[n - i] *= gain;
        }
        fft.inv(s, S);
        double e = 0;
        for (double v : s)
            e += v * v;
        double rms = std::sqrt(e / (double)n);
        for (size_t i = 0; i < n && rms > 0; i++)
            d[a + i] += s[i] * (rms0 + (rms1 - rms0) * (double)i / (double)n) / rms;
    };
    {
        // pink noise as the editor's: a 1 s loop (its spectrum the PINK bands, random phases) with the crest factor
        // brought to 3 (clipped at 3 rms, the spectrum restored, 40 times), repeated with the level ramp
        const size_t n = (size_t)fs, nb = n / 2 + 1;
        vector<double> mag(nb, 0.0);
        for (size_t i = 1; i < nb; i++) {
            double f = (double)i * fs / n, p = 3.0 * std::log2(f / 1000.0) + 17.0, db;
            if (p <= 0)
                db = PINK[0];
            else if (p >= 29)
                db = PINK[29];
            else {
                int k = (int)p;
                db = PINK[k] + (PINK[k + 1] - PINK[k]) * (p - k);
            }
            mag[i] = std::pow(10.0, db / 20.0) / std::sqrt(f);
        }
        Eigen::FFT<double> fft;
        vector<std::complex<double>> X(n);
        for (size_t i = 0; i < nb; i++) {
            double ph = 2 * M_PI * r.uni();
            X[i] = std::polar(mag[i], ph);
            if (i && i < n - i)
                X[n - i] = std::conj(X[i]);
        }
        vector<double> loop;
        fft.inv(loop, X);
        for (int it = 0; it < 40; it++) {
            double e = 0;
            for (double v : loop)
                e += v * v;
            double lim = 3.0 * std::sqrt(e / (double)n);
            for (double &v : loop)
                v = std::max(-lim, std::min(lim, v));
            fft.fwd(X, loop);
            for (size_t i = 0; i < n; i++) {
                size_t k = i < nb ? i : n - i;
                double a = std::abs(X[i]);
                X[i] = a > 0 ? X[i] * (mag[k] / a) : std::complex<double>(0, 0);
            }
            fft.inv(loop, X);
        }
        double e = 0;
        for (double v : loop)
            e += v * v;
        double rms = std::sqrt(e / (double)n);
        const size_t a = (size_t)(30 * fs), len = (size_t)(20 * fs);
        for (size_t i = 0; i < len; i++)
            d[a + i] = loop[i % n] / rms * (0.0378 + (0.0585 - 0.0378) * (double)i / (double)len);
    }
    // guitar: notes of the open strings' scales, chords now and then, a phrase every 0.2 - 0.8 s
    static const double strings[6] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
    auto phrases = [&](double t0, double t1, double rms0, double rms1, uint64_t seed) {
        Rng q(seed);
        vector<double> g(N, 0.0);
        for (double t = t0; t < t1 - 0.3;) {
            bool chord = q.uni() < 0.35;
            int s = (int)(q.uni() * 6), fret = (int)(q.uni() * 13);
            double dur = 0.4 + q.uni() * 1.6, amp = 0.5 + q.uni() * 0.5, bright = 0.3 + q.uni() * 0.6;
            size_t at = (size_t)(t * fs);
            if (chord) {
                static const int shape[3][6] = { { 0, 2, 2, 1, 0, 0 }, { 3, 2, 0, 0, 0, 3 }, { -1, 0, 2, 2, 2, 0 } };
                const int *sh = shape[(int)(q.uni() * 3)];
                for (int k = 0; k < 6; k++)
                    if (sh[k] >= 0)
                        pluck(g, at + (size_t)(k * 0.008 * fs), strings[k] * std::pow(2.0, (sh[k] + fret % 5) / 12.0),
                              amp * 0.6, dur, bright, q);
            } else {
                pluck(g, at, strings[s] * std::pow(2.0, fret / 12.0), amp, dur, bright, q);
            }
            t += 0.2 + q.uni() * 0.6;
        }
        shape(g, t0, t1, GUITAR, rms0, rms1);
    };
    phrases(50.0, 70.0, 0.15, 0.15, 0xC0FFEE02);
    fvec out(N);
    for (size_t i = 0; i < N; i++)
        out[i] = (float)std::max(-1.0, std::min(1.0, d[i]));
    return out;
}

// ------------------------------------------------------------------------------------------------ the fit

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// the pedal's IIR: direct form II in double on x * 1000, (float)((double)(float)y * 0.001) out; state from 0
static void iir_run(fvec &s, size_t n, const Biquad &q, size_t at = 0)
{
    double w1 = 0, w2 = 0;
    for (size_t i = at; i < at + n; i++) {
        double w = (double)s[i] * 1000.0 - q.a1 * w1 - q.a2 * w2;
        float y = (float)(q.b0 * w + q.b1 * w1 + q.b2 * w2);
        s[i] = (float)((double)y * 0.001);
        w2 = w1;
        w1 = w;
    }
}

// exact linear convolution, the output as long as the input (the editor's zero-latency partitioned convolver)
static fvec convolve(const float *x, size_t n, const fvec &h)
{
    fvec y(n, 0.0f);
    const size_t L = h.size();
    for (size_t i = 0; i < n; i++) {
        double acc = 0;
        size_t kmax = std::min(L, i + 1);
        for (size_t k = 0; k < kmax; k++)
            acc += (double)h[k] * x[i - k];
        y[i] = (float)acc;
    }
    return y;
}

// a long convolution by FFT blocks (overlap-add), for the 2048-tap IR over seconds of signal
static fvec convolve_fft(const float *x, size_t n, const fvec &h)
{
    if (h.size() <= 256)
        return convolve(x, n, h);
    const size_t L = h.size(), N = 8192, B = N - L + 1;
    Eigen::FFT<double> fft;
    vector<double> hp(N, 0.0), blk(N);
    for (size_t k = 0; k < L; k++)
        hp[k] = h[k];
    vector<std::complex<double>> H, X;
    fft.fwd(H, hp);
    vector<double> acc(n + N, 0.0), out;
    for (size_t at = 0; at < n; at += B) {
        std::fill(blk.begin(), blk.end(), 0.0);
        size_t cnt = std::min(B, n - at);
        for (size_t i = 0; i < cnt; i++)
            blk[i] = x[at + i];
        fft.fwd(X, blk);
        for (size_t k = 0; k < X.size(); k++)
            X[k] *= H[k];
        fft.inv(out, X);
        for (size_t i = 0; i < N && at + i < acc.size(); i++)
            acc[at + i] += out[i];
    }
    fvec y(n);
    for (size_t i = 0; i < n; i++)
        y[i] = (float)acc[i];
    return y;
}

// half-band polyphase resamplers of first-order all-passes: y = a x + s, s = x - a y
static inline float allpass(float a, float &s, float x)
{
    float y = a * x + s;
    s = x - a * y;
    return y;
}

static const float UP7[7] = { 0.045728147f, 0.332501113f, 0.663202047f, 0.933855832f,
                              0.168087542f, 0.504485726f, 0.803780854f };
static const float UP5[5] = { 0.054230779f, 0.398796976f, 0.862917840f, 0.199699581f, 0.621096849f };
static const float DN6[6] = { 0.054217525f, 0.383087337f, 0.748720944f, 0.196797967f, 0.573136330f, 0.914293706f };
static const float DN4[4] = { 0.070765950f, 0.513167560f, 0.257853091f, 0.817317367f };

struct Sat {
    float apos = 1, aneg = 1, kp = 1, kn = 1;
};

// the Clone's non-linearity at 4x over a signal, the resamplers from rest
static void sat4x(float *s, size_t n, const Sat &p)
{
    float u7[7] = {}, u5[5] = {}, d6[6] = {}, d4[4] = {}, z6 = 0, z4 = 0;
    for (size_t i = 0; i < n; i++) {
        float x = s[i], a = x, b = x;
        for (int k = 0; k < 4; k++)
            a = allpass(UP7[k], u7[k], a);
        for (int k = 4; k < 7; k++)
            b = allpass(UP7[k], u7[k], b);
        float q[4], two[2] = { a, b };
        for (int j = 0; j < 2; j++) {
            float c = two[j], e = two[j];
            for (int k = 0; k < 3; k++)
                c = allpass(UP5[k], u5[k], c);
            for (int k = 3; k < 5; k++)
                e = allpass(UP5[k], u5[k], e);
            q[2 * j] = c;
            q[2 * j + 1] = e;
        }
        for (int j = 0; j < 4; j++) {
            float v = q[j];
            q[j] = v > 0 ? (1.0f - std::exp(-p.kp * v)) * p.apos : (1.0f - std::exp(p.kn * v)) * -p.aneg;
        }
        float o2[2];
        for (int j = 0; j < 2; j++) {
            float c = q[2 * j], e = q[2 * j + 1];
            for (int k = 0; k < 2; k++)
                c = allpass(DN4[k], d4[k], c);
            for (int k = 2; k < 4; k++)
                e = allpass(DN4[k], d4[k], e);
            o2[j] = (c + z4) * 0.5f;
            z4 = e;
        }
        float c = o2[0], e = o2[1];
        for (int k = 0; k < 3; k++)
            c = allpass(DN6[k], d6[k], c);
        for (int k = 3; k < 6; k++)
            e = allpass(DN6[k], d6[k], e);
        s[i] = (c + z6) * 0.5f;
        z6 = e;
    }
}

// MATLAB smoothdata(x, 'gaussian', w): sigma = w / 5, truncated renormalised edges
static fvec smoothdata(const fvec &in, int w)
{
    const int n = (int)in.size();
    fvec out(n, 0.0f);
    if (w < 1)
        return out;
    vector<double> g(w);
    double a = 5.0 / w, c = std::ceil(w * 0.5), S = 0;
    for (int i = 0; i < w; i++) {
        g[i] = std::exp(-0.5 * ((i + 1 - c) * a) * ((i + 1 - c) * a));
        S += g[i];
    }
    int L = w | 1;
    vector<double> k(L, 0.0);
    for (int j = 0; j < w; j++)
        k[j] = g[w - 1 - j] * (1e6 / S);
    int h = (int)std::ceil(L * 0.5) - 1;
    for (int i = 0; i < n; i++) {
        if (i >= h && i < n - h) {
            double acc = 0;
            for (int j = -h; j <= h; j++)
                acc += (double)in[i + j] * k[j + h];
            out[i] = (float)(acc * 1e-6);
        } else {
            int jlo = -std::min(i, h), jhi = i < n - h ? h : n - 1 - i;
            double num = 0, den = 0;
            for (int j = jlo; j <= jhi; j++) {
                num += (double)in[i + j] * k[j + h];
                den += k[j + h];
            }
            out[i] = (float)(num / den);
        }
    }
    return out;
}

// linear interpolation, extrapolated to the right, FLT_MAX left of x[0]
static fvec interp1(const fvec &x, const fvec &y, const fvec &xq)
{
    const size_t nx = x.size();
    fvec out(xq.size(), FMAX);
    if (nx < 2)
        return out;
    fvec sl(nx), ic(nx);
    for (size_t i = 0; i + 1 < nx; i++) {
        sl[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
        ic[i] = y[i] - x[i] * sl[i];
    }
    sl[nx - 1] = sl[nx - 2];
    ic[nx - 1] = ic[nx - 2];
    for (size_t q = 0; q < xq.size(); q++) {
        // the largest x[i] <= xq (x ascending): binary search
        size_t i = (size_t)(std::upper_bound(x.begin(), x.end(), xq[q]) - x.begin());
        if (i == 0)
            continue;
        i--;
        out[q] = xq[q] * sl[i] + ic[i];
    }
    return out;
}

static float mel_of(float f)
{
    return std::log10(f / 700.0f + 1.0f) * 2595.0f;
}

static float hz_of(float m)
{
    return (std::pow(10.0f, m / 2595.0f) - 1.0f) * 700.0f;
}

static fvec lin_acc(float a, float b, int n)
{
    fvec v(n);
    v[0] = a;
    float d = (b - a) / (float)(n - 1);
    for (int i = 1; i < n - 1; i++)
        v[i] = v[i - 1] + d;
    v[n - 1] = b;
    return v;
}

// smooth a magnitude in dB on a mel-warped axis and resample it to m points of a linear axis (outF)
static fvec smooth_freq(const fvec &mag, const fvec &f, int m, fvec &outF)
{
    const int n = (int)mag.size();
    fvec melN = lin_acc(mel_of(f[0]), mel_of(f[n - 1]), n), hzN(n);
    for (int i = 0; i < n; i++)
        hzN[i] = hz_of(melN[i]);
    hzN[0] = f[0];
    hzN[n - 1] = f[n - 1];
    fvec melM = lin_acc(mel_of(f[0]), mel_of(f[n - 1]), m), hzM(m);
    for (int i = 0; i < m; i++)
        hzM[i] = hz_of(melM[i]);
    hzM[0] = f[0];
    hzM[m - 1] = f[n - 1];
    outF = lin_acc(f[0], f[n - 1], m);
    fvec db(n);
    for (int i = 0; i < n; i++)
        db[i] = 20.0f * std::log10(mag[i]);
    fvec t1 = smoothdata(db, (int)(n * 0.002));
    fvec t2 = interp1(f, t1, hzN);
    fvec t3 = smoothdata(t2, (n / m) * 2);
    fvec t4 = n != m ? interp1(melN, t3, melM) : t3;
    fvec t5 = interp1(hzM, t4, outF);
    fvec out(m);
    for (int i = 0; i < m; i++)
        out[i] = std::pow(10.0f, t5[i] * 0.05f);
    return out;
}

// real-cepstrum minimum phase from a one-sided magnitude (p bins), L taps, DC removed, the full energy kept
static fvec min_phase(const fvec &mag, int L)
{
    const int p = (int)mag.size(), N = 2 * p - 2;
    vector<double> X(N);
    for (int i = 0; i < p; i++)
        X[i] = mag[i];
    for (int j = 0; j < p - 2; j++)
        X[p + j] = mag[p - 2 - j];
    double mx = 0;
    for (double v : X)
        mx = std::max(mx, std::fabs(v));
    if (mx != 0) {
        double fl = mx * 1e-5;
        for (double &v : X)
            if (std::fabs(v) < fl)
                v = fl;
    }
    Eigen::FFT<double> fft;
    vector<std::complex<double>> lg(N), c, Y, Z(N);
    for (int i = 0; i < N; i++)
        lg[i] = std::log(X[i] + FLT_EPS);
    fft.inv(c, lg);                                    // inverse DFT, scaled by 1 / N
    vector<std::complex<double>> cf(N, 0.0);
    cf[0] = c[0];
    for (int k = 1; k < N / 2; k++)
        cf[k] = c[k] + std::conj(c[N - k]);
    cf[N / 2] = std::conj(c[N / 2]);
    fft.fwd(Y, cf);
    for (int k = 0; k < N; k++)
        Z[k] = std::exp(Y[k].real()) * std::complex<double>(std::cos(Y[k].imag()), std::sin(Y[k].imag()));
    vector<std::complex<double>> hz;
    fft.inv(hz, Z);
    double E = 0;
    for (int i = 0; i < N; i++)
        E += hz[i].real() * hz[i].real();
    fvec h(L, 0.0f);
    double mean = 0;
    for (int i = 0; i < L && i < N; i++) {
        h[i] = (float)hz[i].real();
        mean += h[i];
    }
    mean /= L;
    double e2 = 0;
    for (int i = 0; i < L; i++) {
        h[i] = (float)(h[i] - mean);
        e2 += (double)h[i] * h[i];
    }
    float g = (float)(std::sqrt(E) / std::sqrt(e2));
    for (float &v : h)
        v *= g;
    return h;
}

struct Fit {
    float fs;
    int nbins = 1025, nfft = 2048, L1 = 128, L2 = 2048, win;
    Biquad iir1, iir2;
    Sat sat;
    fvec h1, h2, A0, Bw, hz512;

    explicit Fit(float rate) : fs(rate)
    {
        win = (int)((double)(fs * 0.125f) + 0.5);
        h1.assign(L1, 0.0f);
        h1[0] = 1;
        h2.assign(L2, 0.0f);
        h2[0] = 1;
        // the 40 Hz Butterworth high pass of IIR 2 (bilinear, float math as the editor's)
        float F2 = fs * fs, den = 177.715805f * fs + F2 + 15791.4521f;
        iir2.b0 = iir2.b2 = (double)((F2 + -9.763315e-25f) / den);
        iir2.b1 = (double)(-(1.95266306e-24f + (F2 + F2)) / den);
        iir2.a1 = (double)(-(-31582.904296875f + (F2 + F2)) / den);
        iir2.a2 = (double)((-177.71580505f * fs + F2 + 15791.4521f) / den);
        // 512 mel-spaced frequencies, 80 Hz .. 10 kHz (the error metric)
        hz512.resize(512);
        float m = 121.95607f;
        hz512[0] = 80.0f;
        for (int i = 1; i < 511; i++) {
            m += 5.77547026f;
            hz512[i] = (std::pow(10.0f, m / 2595.0f) - 1.0f) * 700.0f;
        }
        hz512[511] = 10000.0f;
    }

    // Welch |Pyx| / Pxx: Hamming win, 50 % overlap, segments x 1000 with the mean removed, nfft wrapped
    void tfestimate(const float *x, const float *y, size_t len, fvec &mag, fvec &freq) const
    {
        const int nb = nfft / 2 + 1;
        fvec w(win);
        for (int i = 0; i < win; i++)
            w[i] = 0.54f - 0.46f * std::cos((float)i * 6.2831855f / (float)(win - 1));
        vector<double> pxx(nb, 0.0), sre(nb, 0.0), sim(nb, 0.0), bx(nfft), by(nfft);
        Eigen::FFT<double> fft;
        vector<std::complex<double>> X, Y;
        const long hop = win - (int)(win * 0.5);
        long pos = 0;
        bool last = false;
        fvec sx(win), sy(win);
        for (;;) {
            if (pos >= (long)len - win) {
                last = true;
                pos = (long)len - win;
            }
            float mx = 0, my = 0;
            for (int i = 0; i < win; i++) {
                sx[i] = x[pos + i] * 1000.0f;
                sy[i] = y[pos + i] * 1000.0f;
                mx += sx[i];
                my += sy[i];
            }
            mx /= (float)win;
            my /= (float)win;
            std::fill(bx.begin(), bx.end(), 0.0);
            std::fill(by.begin(), by.end(), 0.0);
            for (int i = 0; i < win; i++) {
                bx[i % nfft] += (sx[i] - mx) * w[i];
                by[i % nfft] += (sy[i] - my) * w[i];
            }
            fft.fwd(X, bx);
            fft.fwd(Y, by);
            for (int k = 0; k < nb; k++) {
                pxx[k] += std::norm(X[k]);
                std::complex<double> s = std::conj(X[k]) * Y[k];
                sre[k] += s.real();
                sim[k] += s.imag();
            }
            pos += hop;
            if (pos >= (long)len || last)
                break;
        }
        mag.resize(nb);
        freq.resize(nb);
        for (int k = 0; k < nb; k++) {
            mag[k] = (float)(std::sqrt(sre[k] * sre[k] + sim[k] * sim[k]) / (pxx[k] + FLT_EPS));
            freq[k] = ((float)k / (float)nfft) * fs;
        }
    }

    // the model: IIR 1 -> IR 1 -> saturation -> IIR 2 (-> IR 2)
    fvec model(const float *x, size_t n, const fvec &ir1, const fvec *ir2) const
    {
        fvec m(x, x + n);
        iir_run(m, n, iir1);
        m = convolve(m.data(), n, ir1);
        sat4x(m.data(), n, sat);
        iir_run(m, n, iir2);
        if (ir2)
            m = convolve_fft(m.data(), n, *ir2);
        return m;
    }

    // moves spectral shape between IR 1 (before the saturation) and IR 2 (after) so the chain follows the target
    void iter_amp(const float *x, const float *target, size_t len, int iters, fvec &H1, fvec &H2, fvec &freq)
    {
        const int n = nbins;
        fvec A = A0, best_A = A0, bIR1 = h1, bIR2 = h2, bH1 = H1, bH2 = H2;
        float best = 100.0f, mu = 1.0f;
        int nlf = (int)std::floor((2.0f / fs) * 60.0f * (float)n);
        for (int it = 0; it < iters; it++) {
            for (int k = 0; k < n; k++) {
                float v = std::pow(A[k], Bw[k] * mu);
                v = std::min(5.0f, v);
                A[k] = (double)v < 0.2 ? 0.2f : v;
            }
            fvec D, D2;
            fvec C = smooth_freq(A, freq, n, D);
            A = smooth_freq(C, D, n, D2);
            freq = D2;
            for (int k = 0; k < n; k++) {
                H2[k] *= A[k];
                H1[k] /= A[k];
            }
            mu *= 0.9f;
            if (nlf >= 2) {
                H1[0] = std::sqrt(std::sqrt(H1[0] * H1[1]) * H1[0]);
                for (int i = 1; i < nlf; i++)
                    H1[i] = std::sqrt(std::sqrt(H1[i - 1] * H1[i + 1]) * H1[i]);
            }
            fvec Df;
            fvec J = smooth_freq(H1, freq, L1, Df);
            h1 = min_phase(J, L1);
            fvec M = model(x, len, h1, nullptr);
            fvec E;
            tfestimate(M.data(), target, len, E, freq);
            fvec Cr(n);
            for (int k = 0; k < n; k++)
                Cr[k] = (float)(((double)E[k] * 1e6) / ((double)H2[k] * 1e6 + 1.1920929e-07));
            fvec Fo;
            A = smooth_freq(Cr, freq, n, Fo);
            freq = Fo;
            h2 = min_phase(E, L2);
            fvec M2 = convolve_fft(M.data(), len, h2);
            fvec F;
            tfestimate(M2.data(), target, len, F, freq);
            fvec Fq = interp1(freq, F, hz512);
            float err = 0;
            for (int k = 0; k < 512; k++)
                err += std::fabs(std::log(Fq[k] + FLT_EPS));
            err *= 1.0f / 512.0f;
            std::fprintf(stderr, "  iteration %d: error %.5f\n", it, err);
            if (err < best) {
                best = err;
                best_A = A;
                bIR1 = h1;
                bIR2 = h2;
                bH1 = H1;
                bH2 = H2;
            } else if ((double)err > 1.2 * best) {
                A = best_A;
                h1 = bIR1;
                h2 = bIR2;
                H1 = bH1;
                H2 = bH2;
                mu *= 0.5f;
            }
        }
        h1 = bIR1;
        h2 = bIR2;
        H1 = bH1;
        H2 = bH2;
    }

    // HTKPA::startClone on x (the DI) and y (the output), both at fs; returns the capture
    vector<uint8_t> run(fvec x, fvec y)
    {
        const size_t N = (size_t)(70.0f * fs + 600.0f);
        x.resize(N, 0.0f);
        y.resize(N, 0.0f);
        // the output's mean and linear trend (float sums, as the editor)
        {
            float s = 0;
            for (size_t i = 0; i < N; i++)
                s += y[i];
            float mean = s / (float)N;
            for (size_t i = 0; i < N; i++)
                y[i] -= mean;
            float St = 0, Sy = 0, Stt = 0, Sty = 0;
            for (size_t i = 0; i < N; i++) {
                float t = (float)(i + 1);
                St += t;
                Sy += y[i];
                Stt += t * t;
                Sty += y[i] * t;
            }
            float Nf = (float)N, slope = (Sty * Nf - Sy * St) / (Stt * Nf - St * St);
            for (size_t i = 0; i < N; i++)
                y[i] -= slope * (float)(i + 1) + (Sy - St * slope) / Nf;
        }
        // latency: the first output sample above 0.01 after the click at 6 s
        const size_t i0 = 6 * (size_t)fs;
        size_t lat = 600;
        for (size_t i = i0; i < i0 + 600; i++)
            if ((double)std::fabs(y[i]) > 0.01) {
                lat = i - i0;
                break;
            }
        const float *yl = y.data() + lat;
        std::fprintf(stderr, "latency %zu samples\n", lat);
        // the static curve: the first 5 s in blocks of 0.1 s, the input's and the output's peaks
        {
            float blk = (float)((double)fs * 0.1);
            int nb = (int)std::floor((float)(int)(5 * fs) / blk);
            fvec A(nb, 0), B(nb, 0), C(nb, 0);
            for (int b = 0; b < nb; b++) {
                size_t j0 = (size_t)((float)b * blk), j1 = (size_t)((float)(b + 1) * blk);
                for (size_t j = j0; j < j1; j++) {
                    A[b] = std::max(A[b], std::fabs(x[j]));
                    B[b] = std::max(B[b], yl[j]);
                    C[b] = std::min(C[b], yl[j]);
                }
            }
            float apos = 0, aneg = 0;
            for (int b = 0; b < nb; b++) {
                apos = std::max(apos, B[b]);
                aneg = std::max(aneg, -C[b]);
            }
            fvec X(2 * nb), Y(2 * nb);
            for (int i = 0; i < nb; i++) {
                X[i] = -A[nb - 1 - i];
                Y[i] = C[nb - 1 - i];
                X[nb + i] = A[i];
                Y[nb + i] = B[i];
            }
            int np = 0, nn = 0;
            for (int b = 0; b < nb && !np; b++)
                if ((double)B[b] >= (double)apos * 0.5)
                    np = b + 1;
            for (int b = 0; b < nb && !nn; b++)
                if ((double)C[b] <= (double)aneg * -0.5)
                    nn = b + 1;
            double sp = 0, sa = 0, sn = 0, san = 0;
            for (int b = 0; b < np; b++) {
                sp += (double)A[b] * B[b];
                sa += (double)A[b] * A[b];
            }
            for (int b = 0; b < nn; b++) {
                sn += (double)(-C[b]) * A[b];
                san += (double)A[b] * A[b];
            }
            float kp = (float)(np ? sp / sa : NAN) / apos, kn = (float)(nn ? sn / san : NAN) / aneg;
            auto err = [&](float cpos, float cneg) {
                float e = 0;
                for (int i = 0; i < 2 * nb; i++) {
                    float v = X[i];
                    float s = v > 0 ? (1.0f - std::exp(cpos * v)) * apos : (1.0f - std::exp(cneg * v)) * -aneg;
                    float d = Y[i] - s;
                    e += d * d;
                }
                return e;
            };
            const float mult[9] = { 0.8f, 0.85f, 0.9f, 0.95f, 1.0f, 1.05f, 1.1f, 1.15f, 1.2f };
            auto search = [&](bool pos) {
                float best = FMAX, lab = 1.0f;
                for (int i = 0; i < 9; i++) {
                    float m = mult[i];
                    float E = pos ? err(m == 1.0f ? -kp : (-m) * kp, kn) : err(-kp, m == 1.0f ? kn : m * kn);
                    if (i == 0) {
                        best = std::min(E, FMAX);
                        lab = E > FMAX ? 1.0f : 0.8f;
                    } else if (!(best < E)) {
                        best = E;
                        lab = m;
                    }
                }
                return lab;
            };
            kp = kp * search(true);
            kn = kn * search(false);
            sat = { apos, aneg, kp, kn };
            std::fprintf(stderr, "saturation: A+ %.6f A- %.6f k+ %.4f k- %.4f\n", apos, aneg, kp, kn);
        }
        const size_t s6 = 6 * (size_t)fs, s23 = 23 * (size_t)fs, s30 = 30 * (size_t)fs, s50 = 50 * (size_t)fs;
        const size_t nA = (size_t)(15 * fs), nB = (size_t)(5 * fs), nC = (size_t)(20 * fs);
        // transfer function of the model without IRs, quiet noise (6..21 s)
        fvec T1, fA;
        {
            fvec a(x.begin() + s6, x.begin() + s6 + nA);
            iir_run(a, nA, iir1);
            sat4x(a.data(), nA, sat);
            iir_run(a, nA, iir2);
            tfestimate(a.data(), yl + s6, nA, T1, fA);
        }
        // the same with loud noise through a cabinet-like FIR (23..28 s)
        fvec T2, fB;
        {
            static const float FIR44[50] = { 2.36821008f, 3.57428002f, 3.09571004f, -0.392399013f, -3.15094995f,
                -3.83070993f, -2.21564007f, -0.882995009f, -0.212944001f, -0.19314f, 0.223961994f, 0.404027998f,
                0.378322005f, 0.0894064009f, 0.193555996f, 0.283547014f, 0.343944997f, 0.0529306009f,
                -0.0379724987f, -0.0676138997f, 0.0733582973f, -0.0264890008f, -0.0229163002f, 0.0236044005f,
                0.085258998f, -0.00927979033f, 0.0438198f, 0.0233258009f, 0.134894997f, 0.00628683995f,
                -0.0259121992f, -0.0195527002f, 0.0853129998f, -0.0184777994f, -0.0766421035f, -0.0785638988f,
                0.0335091017f, 0.0272959992f, -0.0993169025f, -0.0953475013f, 0.0532824993f, 0.0342740007f,
                -0.0936421007f, -0.0669772997f, -0.0113530997f, 0.0399198011f, -0.0771979019f, -0.0979806036f,
                0.00203220011f, 0.0430492982f };
            static const float FIR48[50] = { 2.36980891f, 3.53219938f, 3.38600636f, 0.571152806f, -2.49451208f,
                -3.85038185f, -3.16384506f, -1.51175368f, -0.581693113f, -0.176165596f, -0.153762206f,
                0.266270608f, 0.405162007f, 0.38954851f, 0.113782898f, 0.153672799f, 0.260469586f, 0.356198996f,
                0.202282995f, -0.0178810991f, -0.0592595004f, -0.0305259991f, 0.0732804015f, -0.0387939997f,
                -0.0205725003f, 0.0209499002f, 0.0891723037f, -0.000669899979f, 0.0329804011f, 0.0190768f,
                0.095199503f, 0.0939730033f, -0.0269184001f, -0.0281860009f, 0.0035335999f, 0.0851882026f,
                -0.0284807999f, -0.0766185001f, -0.082170397f, 0.0129757002f, 0.0508528985f, -0.0622742996f,
                -0.122342698f, -0.0220810995f, 0.0803612992f, -0.0201430004f, -0.0981329009f, -0.0581544004f,
                -0.00325089996f, 0.0392660014f };
            static const float FIR96[50] = { 2.36980891f, 3.1448822f, 3.53219938f, 3.68479872f, 3.38600659f,
                2.29713893f, 0.571152806f, -1.17574263f, -2.49451208f, -3.36989093f, -3.85038209f, -3.79510856f,
                -3.16384506f, -2.27035475f, -1.5117538f, -0.987195015f, -0.581693172f, -0.282492012f,
                -0.176165596f, -0.201950803f, -0.153762206f, 0.0479568988f, 0.266270608f, 0.374002993f,
                0.405162007f, 0.421350509f, 0.38954851f, 0.256981611f, 0.113782898f, 0.0814799964f,
                0.153672799f, 0.228178203f, 0.260469586f, 0.303641886f, 0.356198996f, 0.332263112f,
                0.202282995f, 0.0537629016f, -0.0178810991f, -0.0349936001f, -0.0592595004f, -0.0757692009f,
                -0.0305259991f, 0.0492307991f, 0.0732804015f, 0.0194604993f, -0.0387939997f, -0.0440240018f,
                -0.0205725003f, -0.00432030018f };
            const float *fir = fs == 44100.0f ? FIR44 : fs == 96000.0f ? FIR96 : FIR48;
            fvec b = convolve(x.data() + s23, nB, fvec(fir, fir + 50));
            iir_run(b, nB, iir1);
            sat4x(b.data(), nB, sat);
            iir_run(b, nB, iir2);
            tfestimate(b.data(), yl + s23, nB, T2, fB);
        }
        const int n = nbins;
        T2 = smoothdata(smoothdata(T2, (int)(0.001 * n)), (int)(n * 0.005));
        float mx = 0;
        for (float v : T1)
            mx = std::max(mx, v);
        float m = (float)((double)mx * 0.001), m2 = m + m, c = (m2 - m) / (m2 * m2);
        for (int k = 0; k < n; k++)
            if (T2[k] < m2)
                T2[k] = c * T2[k] * T2[k] + m;
        fvec H(n);
        for (int k = 0; k < n; k++)
            H[k] = (float)((double)T1[k] * 1e6 / ((double)T2[k] * 1e6 + 1.1920928955078125e-07));
        // the starting correction (ones) and its per-bin weights: 1 below 80 Hz, down to 0.5 at the top
        A0.assign(n, 1.0f);
        Bw.assign(n, 1.0f);
        {
            int k0 = (int)std::floor((2.0f / fs) * 80.0f * 1025.0f), cnt = 1025 - k0;
            float v = 1.0f, step = 0.5f / (float)cnt;
            Bw[k0 - 1] = 1.0f;
            for (int j = 1; j <= cnt; j++) {
                v -= step;
                Bw[k0 - 1 + j] = j == cnt ? 0.5f : v;
            }
        }
        fvec fq = fB;
        std::fprintf(stderr, "iterAmpCoeff 1 (loud noise)\n");
        iter_amp(x.data() + s23, yl + s23, nB, 3, H, T2, fq);
        std::fprintf(stderr, "iterAmpCoeff 2 (quiet noise)\n");
        iter_amp(x.data() + s6, yl + s6, nA, 2, H, T2, fq);
        std::fprintf(stderr, "iterAmpCoeff 3 (rising noise)\n");
        iter_amp(x.data() + s30, yl + s30, nC, 5, H, T2, fq);
        // the last correction of IR 2: the averaged spectra of the model and the device over 50..70 s
        {
            fvec d(x.begin() + s50, x.begin() + s50 + nC);
            iir_run(d, nC, iir1);
            d = convolve(d.data(), nC, h1);
            sat4x(d.data(), nC, sat);
            iir_run(d, nC, iir2);
            fvec ym = convolve_fft(d.data(), nC, h2);
            const int M = (int)std::ceil((double)fs * 0.1), K = (int)(M * 0.5 + 1.0), P = (int)(nC / M);
            vector<double> avgM(M, 0.0), avgD(M, 0.0);
            for (int p = 0; p < P; p++)
                for (int i = 0; i < M; i++) {
                    avgM[i] += ym[(size_t)p * M + i];
                    avgD[i] += yl[s50 + (size_t)p * M + i];
                }
            double sm = 0, sd = 0;
            for (int i = 0; i < M; i++) {
                sm += avgM[i];
                sd += avgD[i];
            }
            for (int i = 0; i < M; i++) {
                double w = std::cos((double)i * 6.2831855 / (double)(M - 1)) * -0.46 + 0.54;
                avgM[i] = (avgM[i] - sm / M) * w;
                avgD[i] = (avgD[i] - sd / M) * w;
            }
            Eigen::FFT<double> fft;
            vector<std::complex<double>> XM, XD;
            fft.fwd(XM, avgM);
            fft.fwd(XD, avgD);
            fvec magM(K), magD(K), F(K);
            for (int k = 0; k < K; k++) {
                magM[k] = (float)std::abs(XM[k]);
                magD[k] = (float)std::abs(XD[k]);
            }
            F = lin_acc(0.0f, fs * 0.5f, K);
            fvec Fo;
            fvec sM = smooth_freq(magM, F, K, Fo), sD = smooth_freq(magD, F, K, Fo);
            fvec R(K);
            auto clamp = [](float v) { v = std::min(v, 10.0f); return v >= 0.1f ? v : 0.1f; };
            for (int k = 0; k < K; k++)
                R[k] = clamp((float)((double)sD[k] * 1e6 / ((double)sM[k] * 1e6 + 1.1920928955078125e-07)));
            R = smoothdata(R, (int)(K * 0.1));
            for (float &v : R)
                v = clamp(v);
            fvec f256;
            fvec mag256 = smooth_freq(R, F, 256, f256);
            fvec mp = min_phase(mag256, 256);
            fvec acc(L2 + 255, 0.0f);
            for (int i = 0; i < L2; i++)
                for (int j = 0; j < 256; j++)
                    acc[i + j] += mp[j] * h2[i];
            float mean = 0;
            for (int i = 0; i < L2; i++)
                mean += acc[i];
            mean /= (float)L2;
            for (int i = 0; i < L2; i++)
                h2[i] = acc[i] - mean;
            fvec d2 = convolve_fft(d.data(), nC, h2);
            double Em = 0, Ed = 0;
            for (size_t i = 0; i < nC; i++) {
                Em += (double)yl[s50 + i] * yl[s50 + i];
                Ed += (double)d2[i] * d2[i];
            }
            float g = (float)(std::sqrt(Em) / std::sqrt(Ed));
            for (float &v : h2)
                v *= g * 4.0f;
        }
        // the capture
        vector<uint8_t> b(0x2288, 0);
        auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); };
        auto putf = [&](size_t at, float v) { std::memcpy(&b[at], &v, 4); };
        auto putd = [&](size_t at, double v) { std::memcpy(&b[at], &v, 8); };
        put32(0x14, 0x2200);
        const Biquad *q[2] = { &iir1, &iir2 };
        for (int k = 0; k < 2; k++) {
            putd(0x18 + 0x28 * k, q[k]->b0);
            putd(0x20 + 0x28 * k, q[k]->b1);
            putd(0x28 + 0x28 * k, q[k]->b2);
            putd(0x30 + 0x28 * k, q[k]->a1);
            putd(0x38 + 0x28 * k, q[k]->a2);
        }
        putf(0x68, sat.apos);
        putf(0x6c, sat.aneg);
        putf(0x70, sat.kp);
        putf(0x74, sat.kn);
        put32(0x78, 0);
        put32(0x7c, 128);
        put32(0x80, 128);
        put32(0x84, (uint32_t)L2);
        fvec r1 = resample(h1, fs, 44100.0), r2 = resample(h2, fs, 44100.0);
        for (size_t i = 0; i < 128 && i < r1.size(); i++)
            putf(0x88 + 4 * i, r1[i]);
        for (size_t i = 0; i < (size_t)L2 && i < r2.size(); i++)
            putf(0x88 + 512 + 4 * i, r2[i]);
        std::memcpy(&b[0], "VTSI", 4);
        b[4] = 0x88;
        b[5] = 0x22;
        uint16_t crc = 0xFFFF;                          // CRC-16 Modbus over 0x0c..end, stored big-endian at 8
        for (size_t i = 0x0c; i < b.size(); i++) {
            crc ^= b[i];
            for (int k = 0; k < 8; k++)
                crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
        b[8] = (uint8_t)(crc >> 8);
        b[9] = (uint8_t)(crc & 0xff);
        return b;
    }
};

// ------------------------------------------------------------------------------------------------ main

static fvec render(const char *nam_path, const fvec &di44, double slim, double &rate)
{
    auto model = nam::get_dsp(std::filesystem::u8path(nam_path));
    if (slim >= 0.0) {
        auto *s = dynamic_cast<nam::SlimmableModel *>(model.get());
        if (s)
            s->SetSlimmableSize(slim);
    }
    rate = model->GetExpectedSampleRate() > 0 ? model->GetExpectedSampleRate() : 48000.0;
    fvec di = resample(di44, 44100.0, rate);
    const int bs = 1024;
    model->Reset(rate, bs);
    fvec out(di.size());
    vector<NAM_SAMPLE> in(bs, 0), o(bs, 0);
    for (size_t at = 0; at < di.size(); at += bs) {
        size_t cnt = std::min<size_t>(bs, di.size() - at);
        for (size_t i = 0; i < cnt; i++)
            in[i] = di[at + i];
        NAM_SAMPLE *ip = in.data(), *op = o.data();
        model->process(&ip, &op, bs);
        for (size_t i = 0; i < cnt; i++) {
            float v = (float)o[i] * 0.31f;                      // the editor's level, kept at 16 bits
            int q = (int)std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f);
            out[at + i] = (float)q / 32768.0f;
        }
    }
    return out;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: clonefit MODEL.nam DI.wav OUT.clo [--target T.wav] [--target-out T.wav] "
                             "[--slim S]\n");
        return 2;
    }
    const char *target_in = nullptr, *target_out = nullptr, *di_out = nullptr;
    double slim = -1.0;
    for (int i = 4; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--target"))
            target_in = argv[i + 1];
        else if (!std::strcmp(argv[i], "--target-out"))
            target_out = argv[i + 1];
        else if (!std::strcmp(argv[i], "--di-out"))
            di_out = argv[i + 1];
        else if (!std::strcmp(argv[i], "--slim"))
            slim = std::atof(argv[i + 1]);
    }
    Wav di;
    if (!std::strcmp(argv[2], "builtin")) {
        di.rate = 44100.0;
        di.x = make_di();
    } else if (!wav_read(argv[2], di) || di.x.size() < (size_t)(70 * di.rate)) {
        std::fprintf(stderr, "DI: %s is not a WAV of at least 70 s\n", argv[2]);
        return 1;
    }
    if (di_out)
        wav_write16(di_out, di.x, (int)di.rate);
    if (di.rate != 44100.0) {
        di.x = resample(di.x, di.rate, 44100.0);
        di.rate = 44100.0;
    }
    double rate = 0;
    fvec y;
    try {
        if (target_in) {
            Wav t;
            if (!wav_read(target_in, t)) {
                std::fprintf(stderr, "target: %s unreadable\n", target_in);
                return 1;
            }
            rate = t.rate;
            y = t.x;
        } else {
            std::fprintf(stderr, "rendering %s\n", argv[1]);
            y = render(argv[1], di.x, slim, rate);
            if (target_out)
                wav_write16(target_out, y, (int)rate);
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "model: %s\n", e.what());
        return 1;
    }
    fvec x = resample(di.x, 44100.0, rate);
    // the DI at the model's rate, kept at 24 bits as the editor's WAV
    for (float &v : x)
        v = (float)std::lround((double)std::max(-1.0f, std::min(1.0f, v)) * 8388607.0) / 8388608.0f;
    std::fprintf(stderr, "fitting at %.0f Hz\n", rate);
    Fit fit((float)rate);
    vector<uint8_t> blob = fit.run(x, y);
    FILE *f = std::fopen(argv[3], "wb");
    if (!f) {
        std::fprintf(stderr, "%s: cannot write\n", argv[3]);
        return 1;
    }
    std::fwrite(blob.data(), 1, blob.size(), f);
    std::fclose(f);
    std::fprintf(stderr, "wrote %s (%zu bytes)\n", argv[3], blob.size());
    return 0;
}
