#!/usr/bin/env python3
"""Generates small original sound effects for Rockdrift as 16-bit PCM
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


def gen_fire():
    return tone(1100, 0.13, square, amp=0.16, attack=0.001, release=0.06, freq_end=260)


def gen_thrust():
    # A low rumble; the game retriggers it while the engine is on.
    return mix(noise(0.14, amp=0.18, attack=0.01, release=0.06, seed=3),
               tone(70, 0.14, sine, amp=0.16, attack=0.01, release=0.05))


def gen_bang():
    # One explosion sample; the game plays it at three pitches for large,
    # medium and small rocks.
    return mix(noise(0.5, amp=0.3, attack=0.001, release=0.44, seed=5),
               tone(110, 0.4, sine, amp=0.2, attack=0.002, release=0.35, freq_end=45))


def gen_beat():
    # The heartbeat: one thump, played at two pitches, faster as rocks run out.
    return tone(120, 0.09, square, amp=0.3, attack=0.002, release=0.06)


def gen_saucer():
    parts = []
    for i in range(8):
        parts.append(tone(1000 if i % 2 == 0 else 760, 0.045, sine, amp=0.14, attack=0.004, release=0.01))
    return concat(*parts)


def gen_saucerfire():
    return tone(900, 0.1, square, amp=0.14, attack=0.001, release=0.05, freq_end=500)


def gen_death():
    return mix(noise(0.9, amp=0.3, attack=0.001, release=0.8, seed=9),
               tone(180, 0.8, square, amp=0.16, attack=0.002, release=0.7, freq_end=40))


def gen_hyper():
    return tone(200, 0.22, sine, amp=0.26, attack=0.004, release=0.05, freq_end=1600)


def gen_extralife():
    parts = []
    for f in (784, 988, 1175, 1568, 1175, 1568):
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
        "select.wav": gen_select(), "fire.wav": gen_fire(), "thrust.wav": gen_thrust(), "bang.wav": gen_bang(),
        "beat.wav": gen_beat(), "saucer.wav": gen_saucer(), "saucerfire.wav": gen_saucerfire(),
        "death.wav": gen_death(), "hyper.wav": gen_hyper(), "extralife.wav": gen_extralife(),
        "gameover.wav": gen_gameover(),
    }
    for name, samples in sounds.items():
        write_wav(os.path.join(out_dir, name), samples)
    print(f"wrote {len(sounds)} sounds")


if __name__ == "__main__":
    main()
