#!/usr/bin/env python3
"""Generates small original sound effects for Ghost Launcher as 16-bit PCM
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


def gen_coin():
    # The arcade credit chime: a short low note snapping up to a long high
    # one that rings out, with a quiet octave on top for sparkle.
    first = tone(988, 0.07, square, amp=0.20, attack=0.002, release=0.02)
    ring = mix(tone(1319, 0.42, square, amp=0.20, attack=0.002, release=0.36),
               tone(2638, 0.30, sine, amp=0.08, attack=0.002, release=0.26))
    return concat(first, ring)


def gen_move():
    return tone(300, 0.035, square, amp=0.12, attack=0.002, release=0.02)


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "assets", "sounds")
    os.makedirs(out_dir, exist_ok=True)
    sounds = {"coin.wav": gen_coin(), "move.wav": gen_move()}
    for name, samples in sounds.items():
        path = os.path.join(out_dir, name)
        write_wav(path, samples)
        print(f"wrote {path} ({len(samples)/SR:.2f}s)")


if __name__ == "__main__":
    main()
