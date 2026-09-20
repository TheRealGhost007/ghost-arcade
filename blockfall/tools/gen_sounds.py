#!/usr/bin/env python3
"""Generates small original sound effects for Blockfall as 16-bit PCM
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


def gen_move():
    return tone(320, 0.045, square, amp=0.22, attack=0.002, release=0.02)


def gen_rotate():
    return tone(520, 0.06, sine, amp=0.28, attack=0.002, release=0.03, freq_end=680)


def gen_harddrop():
    thud = tone(140, 0.09, square, amp=0.4, attack=0.001, release=0.06, freq_end=60)
    click = tone(900, 0.02, sine, amp=0.15, attack=0.001, release=0.015)
    return mix(thud, click)


def gen_clear():
    notes = [660, 880, 990]
    parts = []
    for f in notes:
        parts.append(tone(f, 0.09, sine, amp=0.3, attack=0.003, release=0.06))
        parts.append(silence(0.02))
    return concat(*parts)


def gen_quad():
    notes = [523, 659, 784, 1046]
    parts = []
    for f in notes:
        parts.append(tone(f, 0.1, sine, amp=0.32, attack=0.003, release=0.05))
        parts.append(silence(0.015))
    parts.append(tone(1318, 0.22, sine, amp=0.35, attack=0.004, release=0.15))
    return concat(*parts)


def gen_levelup():
    notes = [440, 554, 659, 880]
    parts = []
    for f in notes:
        parts.append(tone(f, 0.08, square, amp=0.22, attack=0.003, release=0.04))
    return concat(*parts)


def gen_gameover():
    notes = [392, 349, 293, 220]
    parts = []
    for f in notes:
        parts.append(tone(f, 0.22, sine, amp=0.3, attack=0.005, release=0.12))
    return concat(*parts)


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "assets", "sounds")
    os.makedirs(out_dir, exist_ok=True)
    sounds = {
        "move.wav": gen_move(),
        "rotate.wav": gen_rotate(),
        "harddrop.wav": gen_harddrop(),
        "clear.wav": gen_clear(),
        "quad.wav": gen_quad(),
        "levelup.wav": gen_levelup(),
        "gameover.wav": gen_gameover(),
    }
    for name, samples in sounds.items():
        path = os.path.join(out_dir, name)
        write_wav(path, samples)
        print(f"wrote {path} ({len(samples)/SR:.2f}s)")


if __name__ == "__main__":
    main()
