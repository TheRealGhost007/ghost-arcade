#!/usr/bin/env python3
"""Generates small original sound effects for Ghostmaze as 16-bit PCM
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


def wob(f0, dur, rate, depth, amp=0.16):
    n = int(dur * SR)
    env = envelope_ar(n, 0.01, 0.08)
    out, phase = [], 0.0
    for i in range(n):
        t = i / SR
        phase += 2 * math.pi * (f0 + depth * math.sin(2 * math.pi * rate * t)) / SR
        out.append(sine(phase) * amp * env[i])
    return out


def gen_possess():
    # A whoosh up and in: a rising wobble under a sparkle.
    return mix(wob(220, 0.28, 12, 60, 0.18), tone(440, 0.28, sine, amp=0.1, attack=0.02, release=0.2, freq_end=990))


def gen_release():
    return mix(wob(660, 0.22, 10, 50, 0.16), tone(880, 0.22, sine, amp=0.08, attack=0.01, release=0.18, freq_end=330))


def gen_step():
    return tone(150, 0.03, square, amp=0.07, attack=0.001, release=0.025)


def gen_blocked():
    return tone(120, 0.06, square, amp=0.12, attack=0.001, release=0.04, freq_end=90)


def gen_push():
    return mix(noise(0.12, amp=0.12, attack=0.005, release=0.1, seed=6), tone(80, 0.12, square, amp=0.12, attack=0.003, release=0.08))


def gen_smash():
    return mix(noise(0.32, amp=0.28, attack=0.001, release=0.28, seed=15), tone(150, 0.3, square, amp=0.18, attack=0.001, release=0.24, freq_end=50))


def gen_key():
    return concat(tone(1046, 0.05, square, amp=0.16, attack=0.002, release=0.02), tone(1568, 0.1, square, amp=0.16, attack=0.002, release=0.07))


def gen_door():
    return mix(noise(0.2, amp=0.1, attack=0.02, release=0.16, seed=8), tone(110, 0.22, square, amp=0.12, attack=0.01, release=0.16, freq_end=70))


def gen_gate():
    return concat(tone(196, 0.08, square, amp=0.14, attack=0.002, release=0.05), tone(294, 0.12, square, amp=0.14, attack=0.002, release=0.08))


def gen_bell():
    # A hand bell: two close sines with a long ring-out.
    return mix(tone(880, 0.6, sine, amp=0.12, attack=0.002, release=0.55), tone(1318, 0.5, sine, amp=0.07, attack=0.002, release=0.45),
               tone(2214, 0.3, sine, amp=0.03, attack=0.002, release=0.28))


def gen_caught():
    parts = [tone(f, 0.09, square, amp=0.2, attack=0.002, release=0.04) for f in (784, 659, 523, 392)]
    parts.append(mix(noise(0.35, amp=0.18, attack=0.002, release=0.3, seed=33), tone(196, 0.35, square, amp=0.16, attack=0.004, release=0.28, freq_end=70)))
    return concat(*parts)


def gen_clear():
    parts = [tone(f, 0.09, sine, amp=0.2, attack=0.004, release=0.06) for f in (392, 494, 587, 784)]
    parts.append(tone(988, 0.35, sine, amp=0.2, attack=0.004, release=0.28))
    return concat(*parts)


def gen_extralife():
    parts = [tone(f, 0.07, square, amp=0.18, attack=0.002, release=0.04) for f in (784, 988, 1175, 1568, 1976)]
    return concat(*parts)


def gen_gameover():
    parts = [tone(f, 0.2, sine, amp=0.2, attack=0.004, release=0.12) for f in (330, 262, 196)]
    parts.append(tone(147, 0.6, sine, amp=0.2, attack=0.004, release=0.4, freq_end=90))
    return concat(*parts)


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "sounds")
    os.makedirs(out_dir, exist_ok=True)
    names = ["select", "possess", "release", "step", "blocked", "push", "smash", "key", "door", "gate", "bell", "caught", "clear", "extralife", "gameover"]
    for name in names:
        write_wav(os.path.join(out_dir, name + ".wav"), globals()["gen_" + name]())
    print("wrote %d sounds to %s" % (len(names), out_dir))


if __name__ == "__main__":
    main()
