#!/usr/bin/env python3
"""Generates small original sound effects for Brickburst as 16-bit PCM
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


def gen_launch():
    return tone(300, 0.12, square, amp=0.2, attack=0.002, release=0.05, freq_end=720)


def gen_paddle():
    return tone(220, 0.06, square, amp=0.26, attack=0.001, release=0.03)


def gen_wall():
    return tone(330, 0.035, square, amp=0.14, attack=0.001, release=0.02)


def gen_brick():
    # A dull knock: the brick held.
    return tone(180, 0.05, square, amp=0.2, attack=0.001, release=0.03, freq_end=140)


def gen_break():
    return mix(tone(660, 0.07, square, amp=0.2, attack=0.001, release=0.04, freq_end=990),
               noise(0.05, amp=0.12, release=0.04))


def gen_explode():
    return mix(noise(0.45, amp=0.34, release=0.4),
               tone(120, 0.4, sine, amp=0.35, attack=0.002, release=0.35, freq_end=40))


def gen_powerup():
    parts = []
    for f in (523, 659, 784, 1046):
        parts.append(tone(f, 0.055, square, amp=0.2, attack=0.002, release=0.03))
    return concat(*parts)


def gen_lifelost():
    return tone(440, 0.45, square, amp=0.24, attack=0.003, release=0.15, freq_end=110)


def gen_levelclear():
    parts = []
    for f in (523, 659, 784, 659, 784, 1046):
        parts.append(tone(f, 0.08, square, amp=0.2, attack=0.003, release=0.04))
    parts.append(tone(1318, 0.3, sine, amp=0.3, attack=0.004, release=0.22))
    return concat(*parts)


def gen_gameover():
    notes = [392, 330, 262, 196]
    parts = []
    for f in notes:
        parts.append(tone(f, 0.2, square, amp=0.2, attack=0.005, release=0.1))
    return concat(*parts)


def gen_shot():
    return tone(1500, 0.05, square, amp=0.13, attack=0.001, release=0.04, freq_end=700)


def gen_shield():
    return mix(tone(330, 0.16, square, amp=0.18, attack=0.002, release=0.1, freq_end=880), tone(660, 0.16, sine, amp=0.12, attack=0.002, release=0.1, freq_end=1320))


def gen_catch():
    return tone(260, 0.06, sine, amp=0.2, attack=0.002, release=0.05, freq_end=180)


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "assets", "sounds")
    os.makedirs(out_dir, exist_ok=True)
    sounds = {
        "select.wav": gen_select(), "launch.wav": gen_launch(), "paddle.wav": gen_paddle(),
        "wall.wav": gen_wall(), "brick.wav": gen_brick(), "break.wav": gen_break(),
        "explode.wav": gen_explode(), "powerup.wav": gen_powerup(), "lifelost.wav": gen_lifelost(),
        "levelclear.wav": gen_levelclear(), "gameover.wav": gen_gameover(),
        "shot.wav": gen_shot(), "shield.wav": gen_shield(), "catch.wav": gen_catch(),
    }
    for name, samples in sounds.items():
        path = os.path.join(out_dir, name)
        write_wav(path, samples)
        print(f"wrote {name} ({len(samples)/SR:.2f}s)")


if __name__ == "__main__":
    main()
