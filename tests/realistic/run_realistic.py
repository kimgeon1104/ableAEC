#!/usr/bin/env python3
"""
Realistic offline test for the AbleAEC algorithm.

Builds a vocal (espeak-ng TTS) and a stereo backing track (synthesised drums / bass / pad / arp),
plays the backing track through virtual loudspeakers in a simulated room (image-source method),
records it with a virtual mic together with the vocal, then runs `aec_wav` and measures:

  * ERLE while only the backing track plays (how much of the speaker sound is removed)
  * vocal SER before / after AEC while singing over the track (how clean the vocal is)

usage: run_realistic.py [--check] <aec_wav binary> <output dir> [extra aec_wav options...]
  --check   exit with status 1 if the default (quality, stereo reference) results fall below
            the regression thresholds in THRESHOLDS
requires: numpy, scipy, espeak-ng
"""

import json
import os
import subprocess
import sys
import tempfile

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, lfilter, resample_poly

FS = 48000
DUR = 30.0
N = int(FS * DUR)
VOCAL_START, VOCAL_END = 8.0, 20.0
rng = np.random.default_rng(1234)


# ----------------------------------------------------------------------------------- signals
def tts(text, voice="ko", speed=150, pitch=50):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "v.wav")
        subprocess.run(["espeak-ng", "-v", voice, "-s", str(speed), "-p", str(pitch), "-w", p, text], check=True)
        sr, x = wavfile.read(p)
    x = x.astype(np.float64) / 32768.0
    g = np.gcd(FS, sr)
    return resample_poly(x, FS // g, sr // g)


def make_vocal():
    lines = [
        ("ko", "안녕하세요. 지금은 에코 제거 테스트를 하고 있습니다."),
        ("en", "One two three, testing the acoustic echo canceller."),
        ("ko", "스피커에서 나오는 음악은 지워지고 목소리만 남아야 합니다."),
        ("en", "The backing track should disappear and the voice should stay."),
    ]
    v = np.zeros(N)
    t = VOCAL_START + 0.3
    for i, (lang, text) in enumerate(lines):
        x = tts(text, voice=lang, pitch=45 + 10 * i)
        i0 = int(t * FS)
        i1 = i0 + len(x)
        if i1 > int(VOCAL_END * FS):  # only whole lines that end before VOCAL_END
            break
        v[i0:i1] += x
        t += len(x) / FS + 0.35
    return v / (np.max(np.abs(v)) + 1e-9) * 0.5


def lp(x, fc, order=2):
    b, a = butter(order, fc / (FS / 2))
    return lfilter(b, a, x)


def hp(x, fc, order=2):
    b, a = butter(order, fc / (FS / 2), btype="high")
    return lfilter(b, a, x)


def saw(freq, n, detune=0.0):
    t = np.arange(n) / FS
    ph = (freq * (1 + detune) * t + rng.random()) % 1.0
    return 2 * ph - 1


def make_music():
    """120 bpm, Am-F-C-G, stereo. Returns (2, N)."""
    bpm = 120
    beat = int(FS * 60 / bpm)
    bar = 4 * beat
    L, R = np.zeros(N), np.zeros(N)
    roots = [55.0, 43.65, 65.41, 49.0]  # A1 F1 C2 G1
    chords = [[220, 261.6, 329.6], [174.6, 220, 261.6], [261.6, 329.6, 392], [196, 246.9, 293.7]]

    def add(sig, start, pan):  # pan -1..1
        e = min(N, start + len(sig))
        if start >= N:
            return
        gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        L[start:e] += gl * sig[: e - start]
        R[start:e] += gr * sig[: e - start]

    # drums
    kn = int(0.35 * FS)
    tk = np.arange(kn) / FS
    kick = np.sin(2 * np.pi * np.cumsum(45 + 90 * np.exp(-tk * 30)) / FS) * np.exp(-tk * 9)
    sn = int(0.2 * FS)
    ts = np.arange(sn) / FS
    snare = (hp(rng.standard_normal(sn), 1500) * 0.6 + np.sin(2 * np.pi * 185 * ts)) * np.exp(-ts * 22)
    hn = int(0.06 * FS)
    hat = hp(rng.standard_normal(hn), 7000, 4) * np.exp(-np.arange(hn) / FS * 70)

    for b0 in range(0, N, beat):
        k = (b0 // beat) % 4
        if k in (0, 2):
            add(0.9 * kick, b0, 0.0)
        if k in (1, 3):
            add(0.5 * snare, b0, -0.1)
        add(0.25 * hat, b0, 0.7)
        add(0.15 * hat, b0 + beat // 2, 0.7)

    # bass: 8th notes on chord root
    for b0 in range(0, N, beat // 2):
        f = roots[(b0 // bar) % 4]
        n = beat // 2
        env = np.minimum(1, np.arange(n) / 200) * np.exp(-np.arange(n) / FS * 4)
        add(0.35 * lp(saw(f, n), 400) * env, b0, 0.0)

    # pad: wide stereo (different detune per side)
    for i0 in range(0, N, bar):
        ch = chords[(i0 // bar) % 4]
        n = bar
        env = np.minimum(1, np.arange(n) / (0.3 * FS)) * np.minimum(1, (n - np.arange(n)) / (0.2 * FS))
        sl = sum(saw(f, n, -0.004) for f in ch)
        sr_ = sum(saw(f, n, +0.005) for f in ch)
        add(0.06 * lp(sl, 2500) * env, i0, -1.0)
        add(0.06 * lp(sr_, 2500) * env, i0, 1.0)

    # arpeggio: left
    for b0 in range(0, N, beat // 4):
        ch = chords[(b0 // bar) % 4]
        f = 2 * ch[(b0 // (beat // 4)) % 3]
        n = beat // 4
        t = np.arange(n) / FS
        add(0.12 * np.sign(np.sin(2 * np.pi * f * t)) * np.exp(-t * 18), b0, -0.6)

    m = np.vstack([L, R])
    return m / np.max(np.abs(m)) * 0.7


# ----------------------------------------------------------------------------------- room
def rir(src, mic, room=(6.0, 4.5, 3.0), rt60=0.35, length=0.45, c=343.0):
    """Shoebox image-source RIR with fractional delays."""
    Lx, Ly, Lz = room
    V = Lx * Ly * Lz
    S = 2 * (Lx * Ly + Lx * Lz + Ly * Lz)
    alpha = min(0.9, 0.161 * V / (S * rt60))
    beta = np.sqrt(1 - alpha)
    nmax = int(np.ceil(length * c / min(room))) + 1
    r = np.arange(-nmax, nmax + 1)
    h = np.zeros(int(length * FS) + 64)
    taps = np.arange(-16, 16)
    win = np.hanning(len(taps) + 2)[1:-1]
    for px in (0, 1):
        for py in (0, 1):
            for pz in (0, 1):
                X, Y, Z = np.meshgrid(r, r, r, indexing="ij")
                ix = (1 - 2 * px) * src[0] + 2 * X * Lx
                iy = (1 - 2 * py) * src[1] + 2 * Y * Ly
                iz = (1 - 2 * pz) * src[2] + 2 * Z * Lz
                d = np.sqrt((ix - mic[0]) ** 2 + (iy - mic[1]) ** 2 + (iz - mic[2]) ** 2).ravel()
                refl = (np.abs(X - px) + np.abs(X) + np.abs(Y - py) + np.abs(Y) + np.abs(Z - pz) + np.abs(Z)).ravel()
                t = d / c * FS
                keep = t < len(h) - 20
                t, d, refl = t[keep], d[keep], refl[keep]
                amp = beta ** refl / (4 * np.pi * d)
                n0 = np.floor(t).astype(int)
                frac = t - n0
                for k, tap in enumerate(taps):
                    idx = n0 + tap
                    ok = (idx >= 0) & (idx < len(h))
                    np.add.at(h, idx[ok], (amp * np.sinc(tap - frac) * win[k])[ok])
    # wall/air absorption: progressively darker tail
    return lp(h, 9000, 1)


# ----------------------------------------------------------------------------------- scenario
def simulate(name, music, vocal, stereo_speakers, distortion, noise_db, echo_to_vocal_db, latency):
    mic_pos = (3.0, 2.0, 1.4)
    mouth = (3.0, 1.85, 1.55)
    feed = music.copy()
    if distortion:
        feed = np.tanh(2.0 * feed) / 2.0
    if stereo_speakers:
        spk = [(1.6, 4.1, 1.6), (4.4, 4.1, 1.6)]
        echo = sum(np.convolve(feed[c], rir(spk[c], mic_pos))[:N] for c in range(2))
    else:
        echo = np.convolve(0.5 * (feed[0] + feed[1]), rir((3.0, 4.1, 1.6), mic_pos))[:N]
    echo = np.concatenate([np.zeros(latency), echo])[:N]  # audio-interface out+in latency

    v = np.convolve(vocal, rir(mouth, mic_pos))[:N]
    seg = slice(int(VOCAL_START * FS), int(VOCAL_END * FS))
    gain = 10 ** (echo_to_vocal_db / 20) * np.sqrt(np.mean(v[seg] ** 2)) / np.sqrt(np.mean(echo[seg] ** 2))
    echo *= gain
    noise = 10 ** (noise_db / 20) * rng.standard_normal(N)
    mic = v + echo + noise
    scale = 0.5 / np.max(np.abs(mic))
    return dict(name=name, ref=music, mic=mic * scale, vocal=v * scale, echo=echo * scale, noise=noise * scale)


def power(x):
    return float(np.mean(x ** 2) + 1e-20)


def db(x):
    return float(10 * np.log10(x))


def evaluate(sc, out):
    fs = FS
    a = slice(int(4 * fs), int(VOCAL_START * fs))            # music only, after 4 s of learning
    b = slice(int(VOCAL_START * fs), int(VOCAL_END * fs))     # vocal + music
    c = slice(int((VOCAL_END + 3) * fs), int(DUR * fs))       # music only, 3 s after the vocal ended
    mic, v = sc["mic"], sc["vocal"]
    vocal_active = np.abs(lp(np.abs(v), 20)) > 0.1 * np.max(np.abs(v))  # only judge where the voice is
    vb = vocal_active[b]
    return {
        "ERLE_music_only_dB": db(power(mic[a]) / power(out[a])),
        "ERLE_after_doubletalk_dB": db(power(mic[c]) / power(out[c])),
        "vocal_SER_mic_dB": db(power(v[b][vb]) / power((mic - v)[b][vb])),
        "vocal_SER_out_dB": db(power(v[b][vb]) / power((out - v)[b][vb])),
    }


def write(path, x):
    wavfile.write(path, FS, np.asarray(x, dtype=np.float32).T if np.ndim(x) == 2 else np.asarray(x, dtype=np.float32))


# Regression thresholds for the default configuration (measured values are a few dB higher).
THRESHOLDS = {
    ("1_mono_speaker", "linear", "ERLE_after_doubletalk_dB"): 22.0,
    ("1_mono_speaker", "linear", "vocal_SER_out_dB"): 22.0,
    ("2_stereo_speakers", "linear", "ERLE_after_doubletalk_dB"): 17.0,
    ("2_stereo_speakers", "linear", "vocal_SER_out_dB"): 16.0,
    ("2_stereo_speakers", "clean", "ERLE_after_doubletalk_dB"): 23.0,
    ("3_stereo_distortion_noise", "linear", "ERLE_after_doubletalk_dB"): 13.0,
    ("4_stereo_loud_bleed", "linear", "vocal_SER_out_dB"): 13.0,
}


def main():
    args = sys.argv[1:]
    check = "--check" in args
    if check:
        args.remove("--check")
    aec_bin, outdir, extra = args[0], args[1], args[2:]
    os.makedirs(outdir, exist_ok=True)
    print("generating vocal + backing track ...", flush=True)
    vocal = make_vocal()
    music = make_music()
    lat = 2 * 256 + 60

    scenarios = [
        simulate("1_mono_speaker", music, vocal, False, False, -75, 0, lat),
        simulate("2_stereo_speakers", music, vocal, True, False, -75, 0, lat),
        simulate("3_stereo_distortion_noise", music, vocal, True, True, -60, 0, lat),
        simulate("4_stereo_loud_bleed", music, vocal, True, False, -75, 6, lat),
    ]

    results = {}
    for sc in scenarios:
        d = os.path.join(outdir, sc["name"])
        os.makedirs(d, exist_ok=True)
        write(os.path.join(d, "ref_backing_track.wav"), sc["ref"])
        write(os.path.join(d, "mic.wav"), sc["mic"])
        write(os.path.join(d, "vocal_truth.wav"), sc["vocal"])
        res = {}
        for mode in ("linear", "clean"):
            o = os.path.join(d, f"out_{mode}.wav")
            subprocess.run([aec_bin, os.path.join(d, "ref_backing_track.wav"), os.path.join(d, "mic.wav"), o,
                            "--mode", mode, "--filter", "300"] + extra, check=True, stdout=subprocess.DEVNULL)
            _, out = wavfile.read(o)
            res[mode] = evaluate(sc, out.astype(np.float64))
        results[sc["name"]] = res
        print(f"\n[{sc['name']}]")
        for mode, r in res.items():
            print(f"  {mode:6s}  ERLE music-only {r['ERLE_music_only_dB']:5.1f} dB | after double-talk "
                  f"{r['ERLE_after_doubletalk_dB']:5.1f} dB | vocal SER {r['vocal_SER_mic_dB']:5.1f} -> "
                  f"{r['vocal_SER_out_dB']:5.1f} dB")
    with open(os.path.join(outdir, "results.json"), "w") as f:
        json.dump(results, f, indent=2)

    if check:
        failed = [(k, results[k[0]][k[1]][k[2]], t) for k, t in THRESHOLDS.items() if results[k[0]][k[1]][k[2]] < t]
        for (sc, mode, metric), v, t in failed:
            print(f"FAIL {sc} {mode} {metric}: {v:.1f} < {t:.1f}")
        print("ALL PASSED" if not failed else f"{len(failed)} FAILURE(S)")
        sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
