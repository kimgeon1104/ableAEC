#!/usr/bin/env python3
"""
Voice-chat test (Discord, Zoom, ...): the far end talks in spurts with pauses.

The remote talker (speaker sound = sidechain reference) speaks 1.5-4 s at a time with 2-7 s
pauses; in some scenarios the local user answers during the pauses and the reference carries
low-level noise. Voice-chat apps stop and restart their playback stream between spurts, which
can move the echo a few ms earlier or later at every onset ("jumps"). Without following the jump
the filter has to re-learn the room at each spurt and the remote talker hears themselves.

Per spurt (from the 3rd on, after initial learning) the script measures
  ERLE_onset_dB  first 0.5 s of the spurt
  ERLE_rest_dB   rest of the spurt
and reports the mean over spurts.

usage: run_voicechat.py [--check] <aec_wav binary> <output dir>
requires: numpy, scipy, espeak-ng
"""

import json
import os
import subprocess
import sys

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_realistic as rr  # noqa: E402  (shares TTS, room model and constants)

DUR = 60.0
rr.DUR = DUR
rr.N = N = int(rr.FS * DUR)
FS = rr.FS

NEAR_LINES = ["네 맞아요 저도 그렇게 생각해요", "아 그거 좋네요 다음에 해봐요",
              "잠깐만요 지금 확인해볼게요", "오늘 방송 몇 시에 시작해요"]

# name -> (max delay jump ms, local user answers in the pauses, reference noise in the pauses dBFS)
SCENARIOS = {
    "steady": (0, False, None),
    "jumps_5ms": (5, False, None),
    "jumps_20ms": (20, False, None),
    "jumps_5ms_answers_noise": (5, True, -60),
    "jumps_20ms_answers_noise": (20, True, -60),
}

# (scenario, metric) -> minimum mean over spurts 3.. (linear AEC). Without following the jumps
# the jump scenarios measure about 0 dB at onsets and 2-9 dB for the rest of each spurt.
THRESHOLDS = {
    ("steady", "ERLE_rest_dB"): 20.0,
    ("jumps_5ms", "ERLE_rest_dB"): 20.0,
    ("jumps_5ms", "ERLE_onset_dB"): 6.0,
    ("jumps_20ms", "ERLE_rest_dB"): 19.0,
    ("jumps_5ms_answers_noise", "ERLE_rest_dB"): 17.0,
    ("jumps_20ms_answers_noise", "ERLE_rest_dB"): 13.0,
}


def far_spurts(rng, pause_noise_db):
    x = np.zeros(N)
    segs = []
    t, i = 1.0, 0
    while t < DUR - 4:
        voice, text = rr.FAR_LINES[i % len(rr.FAR_LINES)]
        s = rr.tts(text, voice=voice, speed=160)[: int(rng.uniform(1.5, 4.0) * FS)]
        i0 = int(t * FS)
        i1 = min(N, i0 + len(s))
        x[i0:i1] = s[: i1 - i0]
        segs.append((t, t + (i1 - i0) / FS))
        t += (i1 - i0) / FS + rng.uniform(2.0, 7.0)
        i += 1
    x *= 0.3 / np.max(np.abs(x))
    if pause_noise_db is not None:
        x += 10 ** (pause_noise_db / 20) * rng.standard_normal(N)
    return x, segs


def answers(segs, level):
    v = np.zeros(N)
    for j in range(len(segs) - 1):
        a, b = segs[j][1] + 0.4, segs[j + 1][0] - 0.4
        if b - a < 1.0:
            continue
        s = rr.tts(NEAR_LINES[j % len(NEAR_LINES)], voice="ko", speed=170)[: int((b - a) * FS)]
        i0 = int(a * FS)
        v[i0:i0 + len(s)] += s
    return v * level / (np.sqrt(np.mean(v[v != 0] ** 2)) + 1e-12)


def jump(echo, segs, max_ms):
    """A new random echo delay from each spurt onset on (0 for the first spurt)."""
    if max_ms <= 0:
        return echo
    r = np.random.default_rng(11)
    out = np.zeros_like(echo)
    for j, (a, _) in enumerate(segs):
        d = int(r.uniform(0, max_ms) * FS / 1000) if j > 0 else 0
        i0 = int(a * FS)
        i1 = int(segs[j + 1][0] * FS) if j + 1 < len(segs) else N
        seg = echo[i0:i1]
        out[i0 + d:i1] = seg[: len(seg) - d]
    out[: int(segs[0][0] * FS)] = echo[: int(segs[0][0] * FS)]
    return out


def run(aec_bin, out_dir, name, max_jump_ms, with_answers, pause_noise_db):
    rng = np.random.default_rng(5)
    far, segs = far_spurts(rng, pause_noise_db)
    ref = np.stack([far, far])
    echo = rr.simulate(name, ref, far.copy(), stereo_speakers=True, noise_db=-200)["echo"]
    echo = jump(echo * 0.25 / np.max(np.abs(echo)), segs, max_jump_ms)
    mic = echo + 1e-3 * rng.standard_normal(N)
    if with_answers:
        mic += np.convolve(answers(segs, 10 ** (-12 / 20)), rr.rir(rr.MOUTH, rr.MIC_POS))[:N]

    d = os.path.join(out_dir, name)
    os.makedirs(d, exist_ok=True)
    rr.write(os.path.join(d, "ref_far_end.wav"), ref)
    rr.write(os.path.join(d, "mic.wav"), mic)
    o = os.path.join(d, "out_linear.wav")
    subprocess.run([aec_bin, os.path.join(d, "ref_far_end.wav"), os.path.join(d, "mic.wav"), o, "--mode", "linear"],
                   check=True, stdout=subprocess.DEVNULL)
    _, out = wavfile.read(o)
    out = out.astype(np.float64)

    def erle(t0, t1):
        s = slice(int(t0 * FS), int(t1 * FS))
        return 10 * np.log10(np.mean(mic[s] ** 2) / (np.mean(out[s] ** 2) + 1e-20))

    onset = [erle(a, a + 0.5) for a, b in segs]
    rest = [erle(a + 0.5, b) for a, b in segs]
    return dict(spurts=[round(a, 2) for a, _ in segs], ERLE_onset_per_spurt=onset, ERLE_rest_per_spurt=rest,
                ERLE_onset_dB=float(np.mean(onset[2:])), ERLE_rest_dB=float(np.mean(rest[2:])))


def main():
    args = sys.argv[1:]
    check = "--check" in args
    args = [a for a in args if a != "--check"]
    if len(args) != 2:
        print(__doc__)
        return 2
    aec_bin, out_dir = args
    os.makedirs(out_dir, exist_ok=True)
    results = {}
    for name, (jmp, ans, noise) in SCENARIOS.items():
        r = run(aec_bin, out_dir, name, jmp, ans, noise)
        results[name] = r
        print(f"[{name}] ERLE (mean of spurts 3..): onset {r['ERLE_onset_dB']:5.1f} dB | rest {r['ERLE_rest_dB']:5.1f} dB")
        print("   per spurt onset/rest: " + "  ".join(f"{a:.0f}/{b:.0f}" for a, b in
                                                   zip(r["ERLE_onset_per_spurt"], r["ERLE_rest_per_spurt"])))
    with open(os.path.join(out_dir, "results.json"), "w") as f:
        json.dump(results, f, indent=1)

    if check:
        failed = [(k, v, results[k[0]][k[1]]) for k, v in THRESHOLDS.items() if not results[k[0]][k[1]] >= v]
        for (sc, m), v, got in failed:
            print(f"FAIL {sc} {m}: {got:.1f} < {v:.1f}")
        print("ALL PASSED" if not failed else f"{len(failed)} FAILURE(S)")
        return 1 if failed else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
