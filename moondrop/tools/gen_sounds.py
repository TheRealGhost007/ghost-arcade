#!/usr/bin/env python3
"""Generates small original sound effects for Moondrop as 16-bit PCM
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


def gen_thrust():
    # A short low rumble, retriggered by the game while the engine burns.
    return mix(noise(0.13, amp=0.22, attack=0.004, release=0.09, seed=17),
               tone(70, 0.13, square, amp=0.1, attack=0.004, release=0.09))


def gen_touch():
    return tone(200, 0.05, square, amp=0.16, attack=0.001, release=0.04, freq_end=120)


def gen_land():
    # Three rising notes, then a held one: a safe landing.
    parts = [tone(f, 0.09, square, amp=0.2, attack=0.002, release=0.05) for f in (392, 523, 659)]
    parts.append(tone(784, 0.3, square, amp=0.2, attack=0.002, release=0.2))
    return concat(*parts)


def gen_crash():
    return mix(noise(0.7, amp=0.3, attack=0.001, release=0.6, seed=31),
               tone(180, 0.6, square, amp=0.22, attack=0.001, release=0.4, freq_end=40))


def gen_lowfuel():
    return concat(tone(880, 0.09, square, amp=0.18, attack=0.002, release=0.03), silence(0.05),
                  tone(880, 0.09, square, amp=0.18, attack=0.002, release=0.03), silence(0.05),
                  tone(880, 0.09, square, amp=0.18, attack=0.002, release=0.03))


def gen_newlevel():
    return concat(tone(330, 0.07, square, amp=0.16, attack=0.002, release=0.03),
                  tone(494, 0.12, square, amp=0.16, attack=0.002, release=0.06))


def gen_gameover():
    parts = [tone(f, 0.2, square, amp=0.2, attack=0.004, release=0.12) for f in (330, 262, 196)]
    parts.append(tone(147, 0.6, square, amp=0.2, attack=0.004, release=0.4, freq_end=90))
    return concat(*parts)


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "sounds")
    os.makedirs(out_dir, exist_ok=True)
    names = ["select", "thrust", "touch", "land", "crash", "lowfuel", "newlevel", "gameover"]
    for name in names:
        write_wav(os.path.join(out_dir, name + ".wav"), globals()["gen_" + name]())
    print("wrote %d sounds to %s" % (len(names), out_dir))


if __name__ == "__main__":
    main()
