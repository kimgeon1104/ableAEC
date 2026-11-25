#!/usr/bin/env python3
"""
Realistic offline test matrix for the AbleAEC algorithm.

Far-end (what the loudspeakers play = sidechain reference), one of
  music_pop     drums / bass / pad / arpeggio, 120 bpm, wide stereo
  music_piano   sparse piano ballad, 76 bpm, notes panned by pitch (very tonal, quiet gaps)
  music_edm     four-on-the-floor, long sub-bass kick, pumping supersaw, mastered (soft clip)
  white_noise   independent white noise on L and R
  speech_mono   remote talker (TTS, several voices / languages), same signal on L and R
  speech_two    two remote talkers, one panned left and one right, taking turns

is played through virtual loudspeakers in a simulated room (image-source method) and recorded
by a virtual mic together with a near-end performer (TTS vocal, 8-20 s). `aec_wav` processes
the mic and the script measures

  ERLE_farend_only_dB      4-8 s, far-end only, after 4 s of learning
  ERLE_after_doubletalk_dB 23-30 s, far-end only, after the performer stopped
  ERLE_late_dB             22.5-30 s, echo-active samples only (used for the on/off scenario)
  vocal_SER_mic/out_dB     8-20 s, how close mic / output are to the clean vocal
  t_converge_s             first time the 0.5 s ERLE reaches 15 dB (inf = never in 0-8 s)

ERLE is computed only where the echo is active (speech pauses are skipped).

usage: run_realistic.py [--check] [--only NAME[,NAME]] <aec_wav binary> <output dir> [extra aec_wav options...]
  --check   exit with status 1 if results fall below THRESHOLDS
requires: numpy, scipy, espeak-ng
"""

import functools
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


def make_pop():
    """Pop band: 120 bpm drums / bass / pad / arp, Am-F-C-G, stereo. Returns (2, N)."""
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


def pan_add(L, R, sig, start, pan):  # pan -1..1 (constant power)
    if start >= N:
        return
    e = min(N, start + len(sig))
    gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    L[start:e] += gl * sig[: e - start]
    R[start:e] += gr * sig[: e - start]


def norm_stereo(L, R, peak=0.7):
    m = np.vstack([L, R])
    return m / (np.max(np.abs(m)) + 1e-12) * peak


def make_piano():
    """Sparse piano ballad: arpeggiated chords, decaying harmonic tones panned by pitch."""
    bpm = 76
    eighth = int(FS * 30 / bpm)
    L, R = np.zeros(N), np.zeros(N)
    prog = [[57, 60, 64, 69], [53, 57, 60, 65], [48, 55, 60, 64], [55, 59, 62, 67]]  # MIDI: Am F C G
    bass = [45, 41, 36, 43]
    n_note = int(2.5 * FS)
    t = np.arange(n_note) / FS

    def tone(midi, vel):
        f0 = 440.0 * 2 ** ((midi - 69) / 12)
        x = np.zeros(n_note)
        for k in range(1, 10):
            fk = f0 * k * (1 + 0.0004 * k * k)  # slight inharmonicity
            if fk > 12000:
                break
            x += (1.0 / k) * np.sin(2 * np.pi * fk * t) * np.exp(-t * (1.2 + 0.7 * k))
        attack = np.minimum(1, np.arange(n_note) / (0.004 * FS))
        return vel * x * attack

    for i, s0 in enumerate(range(0, N, eighth)):
        bar = i // 8
        chord = prog[bar % 4]
        if i % 8 == 0:
            pan_add(L, R, tone(bass[bar % 4], 0.55), s0, -0.3)
        if rng.random() < 0.85:  # occasional rests
            note = chord[[0, 1, 2, 3, 2, 1, 3, 2][i % 8]] + (12 if i % 8 in (3, 6) else 0)
            pan_add(L, R, tone(note, 0.25 + 0.15 * rng.random()), s0, np.clip((note - 64) / 12, -0.9, 0.9))
    return norm_stereo(L, R)


def make_edm():
    """128 bpm four-on-the-floor, sub kick, pumping supersaw chords, offbeat hats, soft-clip master."""
    bpm = 128
    beat = int(FS * 60 / bpm)
    bar = 4 * beat
    L, R = np.zeros(N), np.zeros(N)
    kn = int(0.45 * FS)
    tk = np.arange(kn) / FS
    kick = np.sin(2 * np.pi * np.cumsum(48 + 140 * np.exp(-tk * 35)) / FS) * np.exp(-tk * 5)
    hn = int(0.12 * FS)
    hat = hp(rng.standard_normal(hn), 6000, 4) * np.exp(-np.arange(hn) / FS * 25)
    cn = int(0.15 * FS)
    clap = hp(rng.standard_normal(cn), 900) * np.exp(-np.arange(cn) / FS * 30)
    chords = [[220, 261.6, 329.6], [174.6, 220, 261.6], [261.6, 329.6, 392], [196, 246.9, 293.7]]
    subs = [55.0, 43.65, 65.41, 49.0]
    # sidechain pumping envelope (ducks right after each kick)
    ph = (np.arange(N) % beat) / beat
    pump = 0.25 + 0.75 * np.minimum(1, ph / 0.45)
    for b0 in range(0, N, beat):
        pan_add(L, R, 1.0 * kick, b0, 0.0)
        pan_add(L, R, 0.22 * hat, b0 + beat // 2, 0.5)
        if (b0 // beat) % 2 == 1:
            pan_add(L, R, 0.35 * clap, b0, -0.2)
    for i0 in range(0, N, bar):
        n = min(bar, N - i0)
        k = (i0 // bar) % 4
        tt = np.arange(n) / FS
        sub = 0.45 * np.sin(2 * np.pi * subs[k] * tt)
        sl = sum(saw(f, n, d) for f in chords[k] for d in (-0.011, -0.004, 0.006))
        sr_ = sum(saw(f, n, d) for f in chords[k] for d in (-0.007, 0.003, 0.012))
        seg = slice(i0, i0 + n)
        L[seg] += (sub + 0.05 * lp(sl, 5000)) * pump[seg]
        R[seg] += (sub + 0.05 * lp(sr_, 5000)) * pump[seg]
    m = norm_stereo(L, R, 1.0)
    return np.tanh(1.8 * m) / np.tanh(1.8) * 0.7  # "mastered": loud, slightly saturated


def make_white():
    return norm_stereo(rng.standard_normal(N), rng.standard_normal(N), 0.7)


FAR_LINES = [
    ("en+m3", "Hello, can you hear me? I think the connection is a little unstable today."),
    ("ko+f2", "네 잘 들려요. 오늘 회의는 다음 주 일정부터 이야기해 볼까요?"),
    ("en+f3", "Sure. Let's start with the release plan and then go through the open issues."),
    ("ko+m2", "좋습니다. 먼저 지난번에 말씀드린 녹음 품질 문제부터 확인해 보겠습니다."),
    ("en+m5", "The echo canceller should remove my voice from your microphone completely."),
    ("ko+f4", "스피커로 나오는 목소리가 마이크로 다시 들어가면 상대방은 자기 목소리를 듣게 됩니다."),
    ("en+f2", "Exactly, that is why we test it with many different voices and languages."),
]


def speech_stream(lines, gap=0.45, start=0.2):
    """Concatenate TTS lines (repeating) to fill the whole duration. Returns mono (N,)."""
    x = np.zeros(N)
    t, i = start, 0
    while t < DUR - 0.5:
        voice, text = lines[i % len(lines)]
        s = tts(text, voice=voice, speed=160)
        i0 = int(t * FS)
        i1 = min(N, i0 + len(s))
        x[i0:i1] += s[: i1 - i0]
        t += len(s) / FS + gap
        i += 1
    return x


def make_speech_mono():
    x = speech_stream(FAR_LINES)
    return norm_stereo(x, x)


def make_speech_two():
    """Two talkers taking turns, talker A on the left speaker, talker B on the right."""
    a_lines = [l for i, l in enumerate(FAR_LINES) if i % 2 == 0]
    b_lines = [l for i, l in enumerate(FAR_LINES) if i % 2 == 1]
    L, R = np.zeros(N), np.zeros(N)
    t, i = 0.2, 0
    while t < DUR - 0.5:
        lines = a_lines if i % 2 == 0 else b_lines
        voice, text = lines[(i // 2) % len(lines)]
        s = tts(text, voice=voice, speed=165)
        i0 = int(t * FS)
        i1 = min(N, i0 + len(s))
        (L if i % 2 == 0 else R)[i0:i1] += s[: i1 - i0]
        t += len(s) / FS - 0.3  # slight overlap between turns
        i += 1
    return norm_stereo(L, R)


FAR_END = {
    "music_pop": make_pop,
    "music_piano": make_piano,
    "music_edm": make_edm,
    "white_noise": make_white,
    "speech_mono": make_speech_mono,
    "speech_two": make_speech_two,
}


# ----------------------------------------------------------------------------------- room
@functools.lru_cache(maxsize=None)
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
MIC_POS = (3.0, 2.0, 1.4)
MOUTH = (3.0, 1.85, 1.55)
SPK_STEREO = ((1.6, 4.1, 1.6), (4.4, 4.1, 1.6))
SPK_MONO = (3.0, 4.1, 1.6)


def simulate(name, far, vocal, stereo_speakers=True, distortion=False, noise_db=-75, echo_to_vocal_db=0.0,
             latency=2 * 256 + 60, echo_segments=None, rt60=0.35):
    """echo_to_vocal_db=None: the loudspeakers do not reach the mic at all.
    echo_segments: [(start_s, end_s), ...] where the echo is audible (e.g. performer moving away)."""
    feed = far.copy()
    if distortion:
        feed = np.tanh(2.0 * feed) / 2.0
    if stereo_speakers:
        echo = sum(np.convolve(feed[c], rir(SPK_STEREO[c], MIC_POS, rt60=rt60, length=1.3 * rt60))[:N] for c in range(2))
    else:
        echo = np.convolve(0.5 * (feed[0] + feed[1]), rir(SPK_MONO, MIC_POS, rt60=rt60, length=1.3 * rt60))[:N]
    echo = np.concatenate([np.zeros(latency), echo])[:N]  # audio-interface out+in latency

    v = np.convolve(vocal, rir(MOUTH, MIC_POS))[:N]
    seg = slice(int(VOCAL_START * FS), int(VOCAL_END * FS))
    gain = 0.0 if echo_to_vocal_db is None else \
        10 ** (echo_to_vocal_db / 20) * np.sqrt(np.mean(v[seg] ** 2)) / np.sqrt(np.mean(echo[seg] ** 2))
    echo *= gain
    if echo_segments:
        g = np.zeros(N)
        for a, b in echo_segments:
            g[int(a * FS):int(b * FS)] = 1.0
        k = int(0.05 * FS)
        echo *= np.convolve(g, np.ones(k) / k, "same")  # 50 ms fades
    noise = 10 ** (noise_db / 20) * np.random.default_rng(7).standard_normal(N)
    mic = v + echo + noise
    scale = 0.5 / np.max(np.abs(mic))
    return dict(name=name, ref=far, mic=mic * scale, vocal=v * scale, echo=echo * scale)


def power(x):
    return float(np.mean(x ** 2) + 1e-20)


def db(x):
    return float(10 * np.log10(x))


def evaluate(sc, out):
    fs = FS
    mic, v, echo = sc["mic"], sc["vocal"], sc["echo"]
    env = lp(np.abs(echo), 10)
    echo_on = env > 0.05 * max(np.max(env), 1e-12)            # skip far-end pauses
    vocal_on = lp(np.abs(v), 20) > 0.1 * np.max(np.abs(v))     # only judge where the voice is

    def erle(a, b):
        m = np.zeros(N, bool)
        m[int(a * fs):int(b * fs)] = True
        m &= echo_on & ~vocal_on
        return db(power(mic[m]) / power(out[m])) if m.sum() > fs // 10 else float("nan")

    t_conv = float("inf")
    for k in range(16):  # 0.5 s windows in 0-8 s
        if erle(k * 0.5, k * 0.5 + 0.5) >= 15.0:
            t_conv = k * 0.5 + 0.5
            break
    b = slice(int(VOCAL_START * fs), int(VOCAL_END * fs))
    vb = vocal_on[b]
    return {
        "ERLE_farend_only_dB": erle(4, VOCAL_START),
        "ERLE_late_dB": erle(22.5, DUR),  # echo-active part after the voice and its reverb are gone
        "ERLE_after_doubletalk_dB": erle(VOCAL_END + 3, DUR),
        "vocal_SER_mic_dB": db(power(v[b][vb]) / power((mic - v)[b][vb])),
        "vocal_SER_out_dB": db(power(v[b][vb]) / power((out - v)[b][vb])),
        "t_converge_s": t_conv,
    }


def write(path, x):
    wavfile.write(path, FS, np.asarray(x, dtype=np.float32).T if np.ndim(x) == 2 else np.asarray(x, dtype=np.float32))


# (name, far-end, options for simulate)
SCENARIOS = [
    ("music_pop__stereo", "music_pop", {}),
    ("music_pop__mono_speaker", "music_pop", dict(stereo_speakers=False)),
    ("music_pop__distortion_noise", "music_pop", dict(distortion=True, noise_db=-60)),
    ("music_pop__loud_bleed", "music_pop", dict(echo_to_vocal_db=6.0)),
    ("music_piano__stereo", "music_piano", {}),
    ("music_edm__stereo", "music_edm", {}),
    ("music_edm__loud_bleed", "music_edm", dict(echo_to_vocal_db=6.0)),
    ("white_noise__stereo", "white_noise", {}),
    ("white_noise__distortion_noise", "white_noise", dict(distortion=True, noise_db=-60)),
    ("speech_mono__stereo", "speech_mono", {}),
    ("speech_mono__mono_speaker", "speech_mono", dict(stereo_speakers=False)),
    ("speech_two__stereo", "speech_two", {}),
    # the loudspeakers barely / not / only sometimes reach the mic (far away, out of the mic's pickup)
    ("music_pop__weak_echo", "music_pop", dict(echo_to_vocal_db=-20.0)),
    ("music_pop__no_echo", "music_pop", dict(echo_to_vocal_db=None)),
    ("music_pop__echo_on_off", "music_pop", dict(echo_segments=[(0, 5), (10, 14), (22, 26)])),
    ("speech_mono__weak_echo", "speech_mono", dict(echo_to_vocal_db=-20.0)),
    ("speech_mono__no_echo", "speech_mono", dict(echo_to_vocal_db=None)),
    # room size: a reverberant room needs a long filter, a dry room a short one
    ("music_pop__reverb_room", "music_pop", dict(rt60=0.8)),
    ("music_pop__dry_room", "music_pop", dict(rt60=0.15)),
]

# Regression thresholds for the default configuration (Quality, stereo reference); a few dB of margin.
THRESHOLDS = {
    ("music_pop__stereo", "linear", "ERLE_after_doubletalk_dB"): 23.0,
    ("music_pop__stereo", "linear", "vocal_SER_out_dB"): 18.0,
    ("music_pop__mono_speaker", "linear", "ERLE_after_doubletalk_dB"): 24.0,
    ("music_pop__distortion_noise", "linear", "ERLE_after_doubletalk_dB"): 14.0,
    ("music_pop__loud_bleed", "linear", "vocal_SER_out_dB"): 15.0,
    ("music_piano__stereo", "linear", "ERLE_after_doubletalk_dB"): 13.0,
    ("music_edm__stereo", "linear", "ERLE_after_doubletalk_dB"): 22.0,
    ("music_edm__loud_bleed", "linear", "ERLE_after_doubletalk_dB"): 21.0,
    ("white_noise__stereo", "linear", "ERLE_after_doubletalk_dB"): 29.0,
    ("white_noise__distortion_noise", "linear", "ERLE_after_doubletalk_dB"): 17.0,
    ("speech_mono__stereo", "linear", "ERLE_after_doubletalk_dB"): 21.0,
    ("speech_mono__mono_speaker", "linear", "ERLE_after_doubletalk_dB"): 21.0,
    ("speech_two__stereo", "linear", "ERLE_after_doubletalk_dB"): 17.0,
    # echo barely / not reaching the mic: must not damage the voice
    ("music_pop__no_echo", "linear", "vocal_SER_out_dB"): 44.0,
    ("music_pop__no_echo", "clean", "vocal_SER_out_dB"): 44.0,
    ("speech_mono__no_echo", "linear", "vocal_SER_out_dB"): 44.0,
    ("music_pop__weak_echo", "linear", "vocal_SER_out_dB"): 17.0,
    ("music_pop__weak_echo", "linear", "ERLE_late_dB"): 12.0,
    ("speech_mono__weak_echo", "linear", "vocal_SER_out_dB"): 15.0,
    ("speech_mono__weak_echo", "linear", "ERLE_late_dB"): 11.0,
}


def main():
    args = sys.argv[1:]
    check = "--check" in args
    if check:
        args.remove("--check")
    only = None
    if "--only" in args:
        i = args.index("--only")
        only = set(args[i + 1].split(","))
        del args[i:i + 2]
    aec_bin, outdir, extra = args[0], args[1], args[2:]
    os.makedirs(outdir, exist_ok=True)

    scenarios = [s for s in SCENARIOS if only is None or s[0] in only or s[1] in only]
    print("generating near-end vocal ...", flush=True)
    vocal = make_vocal()
    far_cache = {}

    results = {}
    for name, far_name, opts in scenarios:
        if far_name not in far_cache:
            print(f"generating far-end '{far_name}' ...", flush=True)
            far_cache[far_name] = FAR_END[far_name]()
        sc = simulate(name, far_cache[far_name], vocal, **opts)
        d = os.path.join(outdir, name)
        os.makedirs(d, exist_ok=True)
        write(os.path.join(d, "ref_far_end.wav"), sc["ref"])
        write(os.path.join(d, "mic.wav"), sc["mic"])
        write(os.path.join(d, "vocal_truth.wav"), sc["vocal"])
        res = {}
        for mode in ("linear", "clean"):
            o = os.path.join(d, f"out_{mode}.wav")
            subprocess.run([aec_bin, os.path.join(d, "ref_far_end.wav"), os.path.join(d, "mic.wav"), o,
                            "--mode", mode, "--filter", "300"] + extra, check=True, stdout=subprocess.DEVNULL)
            _, out = wavfile.read(o)
            res[mode] = evaluate(sc, out.astype(np.float64))
        results[name] = res
        print(f"[{name}]")
        for mode, r in res.items():
            print(f"  {mode:6s}  ERLE far-end only {r['ERLE_farend_only_dB']:5.1f} dB | after double-talk "
                  f"{r['ERLE_after_doubletalk_dB']:5.1f} dB | vocal SER {r['vocal_SER_mic_dB']:5.1f} -> "
                  f"{r['vocal_SER_out_dB']:5.1f} dB | 15 dB after {r['t_converge_s']:.1f} s", flush=True)
    with open(os.path.join(outdir, "results.json"), "w") as f:
        json.dump(results, f, indent=2)

    if check:
        failed = []
        for (sc, mode, metric), t in THRESHOLDS.items():
            if sc in results and not results[sc][mode][metric] >= t:
                failed.append((sc, mode, metric, results[sc][mode][metric], t))
        for sc, mode, metric, v, t in failed:
            print(f"FAIL {sc} {mode} {metric}: {v:.1f} < {t:.1f}")
        print("ALL PASSED" if not failed else f"{len(failed)} FAILURE(S)")
        sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
