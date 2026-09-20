#!/usr/bin/env python3
"""Generates small original sound effects for Lanehop as 16-bit PCM
WAV files. Every sound is synthesized from scratch (sine/square oscillators
with simple envelopes) -- nothing sampled or copied. Uses only the Python
standard library.
"""
import math
import struct
import wave
import os

SR = 44100


def envelope_ar(n, attack, release):
    out = []
    a = max(1, int(attack * SR))
    r = max(1, int(release * SR))
    for i in range(n):
        if i < a:
            out.append(i / a)
        elif i >= n - r:
            out.append(max(0.0, (n - i) / r))
        else:
            out.append(1.0)
    return out


def tone(freq, dur, wave_fn, amp=0.35, attack=0.005, release=0.05, freq_end=None):
    n = int(dur * SR)
    env = envelope_ar(n, attack, release)
    samples = []
    for i in range(n):
        t = i / SR
        f = freq if freq_end is None else freq + (freq_end - freq) * (i / max(1, n - 1))
        phase = 2 * math.pi * f * t
        samples.append(wave_fn(phase) * amp * env[i])
    return samples


def sine(phase):
    return math.sin(phase)


def square(phase):
    return 1.0 if math.sin(phase) >= 0 else -1.0


def mix(*tracks):
    length = max(len(t) for t in tracks)
    out = [0.0] * length
    for t in tracks:
        for i, v in enumerate(t):
            out[i] += v
    peak = max(1.0, max(abs(v) for v in out))
    if peak > 1.0:
        out = [v / peak for v in out]
    return out


def concat(*tracks):
    out = []
    for t in tracks:
        out.extend(t)
    return out


def silence(dur):
    return [0.0] * int(dur * SR)


def write_wav(path, samples):
    with wave.open(path, "w") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SR)
        frames = b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples)
        f.writeframes(frames)


def noise(dur, amp=0.3, attack=0.001, release=0.1, seed=7):
    # Deterministic white noise (an LCG, so the file is identical every run).
    n = int(dur * SR)
    env = envelope_ar(n, attack, release)
    out, state = [], seed
    for i in range(n):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        out.append(((state / 0x3FFFFFFF) - 1.0) * amp * env[i])
    return out


def gen_select():
    return tone(360, 0.04, square, amp=0.18, attack=0.002, release=0.02)


def gen_hop():
    # A springy boing: a quick rising blip.
    return tone(300, 0.075, square, amp=0.2, attack=0.002, release=0.03, freq_end=620)


def gen_splat():
    return mix(noise(0.22, amp=0.32, attack=0.001, release=0.2, seed=13),
               tone(150, 0.2, square, amp=0.22, attack=0.001, release=0.15, freq_end=50))


def gen_splash():
    return mix(noise(0.4, amp=0.2, attack=0.02, release=0.36, seed=21),
               tone(420, 0.25, sine, amp=0.14, attack=0.002, release=0.2, freq_end=140))


def gen_home():
    parts = []
    for f in (523, 659, 784, 1046):
        parts.append(tone(f, 0.075, square, amp=0.2, attack=0.002, release=0.04))
    return concat(*parts)


def gen_fly():
    parts = []
    for f in (1318, 1568, 2093):
        parts.append(tone(f, 0.05, sine, amp=0.22, attack=0.002, release=0.03))
    return concat(*parts)


def gen_tick():
    return tone(880, 0.05, square, amp=0.16, attack=0.001, release=0.03)


def gen_levelclear():
    parts = []
    for f in (523, 659, 784, 1046, 784, 1046, 1318):
        parts.append(tone(f, 0.09, square, amp=0.2, attack=0.003, release=0.04))
    parts.append(tone(1568, 0.3, sine, amp=0.3, attack=0.004, release=0.22))
    return concat(*parts)


def gen_extralife():
    parts = []
    for f in (659, 784, 988, 1318, 988, 1318):
        parts.append(tone(f, 0.07, sine, amp=0.26, attack=0.003, release=0.04))
    return concat(*parts)


def gen_gameover():
    parts = []
    for f in (330, 262, 220, 165):
        parts.append(tone(f, 0.24, square, amp=0.2, attack=0.005, release=0.12))
    return concat(*parts)


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "assets", "sounds")
    os.makedirs(out_dir, exist_ok=True)
    sounds = {
        "select.wav": gen_select(), "hop.wav": gen_hop(), "splat.wav": gen_splat(), "splash.wav": gen_splash(),
        "home.wav": gen_home(), "fly.wav": gen_fly(), "tick.wav": gen_tick(),
        "levelclear.wav": gen_levelclear(), "extralife.wav": gen_extralife(), "gameover.wav": gen_gameover(),
    }
    for name, samples in sounds.items():
        write_wav(os.path.join(out_dir, name), samples)
    print(f"wrote {len(sounds)} sounds")


if __name__ == "__main__":
    main()
