#!/usr/bin/env python3
"""Generates small original sound effects for Blockfall as 16-bit PCM
WAV files. Every sound is synthesized from scratch (sine/square oscillators,
a deterministic noise burst and simple envelopes) -- nothing sampled or
copied. Uses only the Python standard library. Deterministic: running it
again writes the same bytes, so the seven 1.0.0 sounds never change.

The 3.1.0 sound pass added the rest (src/soundplan.h names them in the
same order as main.c's table): a soft lock tick, the hold swoosh, the five
power-ups (bomb, laser, freeze with its thaw tick, slow with its wind-up at
the end, sweep) and the back-to-back sparkle. (3.1.0's danger warning went
in 3.6.1: danger plays the shared WARNING jingle from ghost-common/sfx.c.)
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


# ---------------------------------------------------------- 3.1.0 sounds

def noise(dur, amp=0.3, attack=0.001, release=0.03, seed=0x9E3779B9):
    """A deterministic noise burst (an LCG, so every run is the same)."""
    n = int(dur * SR)
    env = envelope_ar(n, attack, release)
    state = seed
    out = []
    for i in range(n):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        v = ((state >> 8) & 0xFFFF) / 32768.0 - 1.0
        out.append(v * amp * env[i])
    return out


def tremolo(track, hz, depth=0.5):
    return [v * (1.0 - depth * 0.5 * (1.0 + math.sin(2 * math.pi * hz * i / SR))) for i, v in enumerate(track)]


def gen_lock():
    """A natural lock: a soft, low tick (the hard drop keeps its thud)."""
    tick = tone(260, 0.035, sine, amp=0.26, attack=0.001, release=0.025, freq_end=180)
    click = tone(1200, 0.012, sine, amp=0.10, attack=0.001, release=0.008)
    return mix(tick, click)


def gen_hold():
    """The piece flying into Hold: a quick swoosh up and back."""
    up = tone(300, 0.07, sine, amp=0.24, attack=0.004, release=0.02, freq_end=900)
    down = tone(900, 0.07, sine, amp=0.20, attack=0.002, release=0.05, freq_end=520)
    body = concat(up, down)
    air = noise(0.14, amp=0.06, attack=0.03, release=0.08, seed=77)
    return mix(body, air)


def gen_bomb():
    """A short fuse (two ticks over the cell's blink), then the boom."""
    fuse = concat(tone(1500, 0.02, square, amp=0.12, attack=0.001, release=0.01), silence(0.04),
                  tone(1800, 0.02, square, amp=0.12, attack=0.001, release=0.01), silence(0.04))
    boom = tone(95, 0.30, square, amp=0.42, attack=0.002, release=0.22, freq_end=32)
    thud = tone(60, 0.22, sine, amp=0.35, attack=0.001, release=0.18, freq_end=40)
    crack = noise(0.09, amp=0.28, attack=0.001, release=0.07, seed=3)
    return concat(fuse, mix(boom, thud, crack))


def gen_laser():
    """The charge running down the column, then the beam."""
    charge = tone(400, 0.12, sine, amp=0.20, attack=0.005, release=0.01, freq_end=1700)
    beam = tremolo(tone(1800, 0.26, square, amp=0.22, attack=0.002, release=0.18, freq_end=1500), 38.0, depth=0.6)
    sub = tone(220, 0.26, sine, amp=0.16, attack=0.002, release=0.2, freq_end=160)
    return concat(charge, mix(beam, sub))


def gen_freeze():
    """Ice: three falling bell tones with a shimmer over them."""
    bells = concat(tone(1568, 0.09, sine, amp=0.26, attack=0.002, release=0.07),
                   tone(1318, 0.09, sine, amp=0.26, attack=0.002, release=0.07),
                   tone(1046, 0.20, sine, amp=0.28, attack=0.002, release=0.16))
    shimmer = tremolo(tone(3136, 0.38, sine, amp=0.08, attack=0.01, release=0.2), 22.0, depth=0.8)
    return mix(bells, shimmer)


def gen_thaw():
    """The frost breaking: a crack and a short falling tick."""
    crack = noise(0.03, amp=0.26, attack=0.001, release=0.02, seed=11)
    tick = tone(900, 0.06, sine, amp=0.22, attack=0.001, release=0.04, freq_end=480)
    return concat(crack, tick)


def gen_slow():
    """Time thickening: a slide down with a low square under it."""
    slide = tone(700, 0.36, sine, amp=0.28, attack=0.004, release=0.2, freq_end=240)
    under = tone(350, 0.36, square, amp=0.08, attack=0.004, release=0.2, freq_end=120)
    return mix(slide, under)


def gen_windup():
    """Slow ending: a wind-up back to speed, with a flutter."""
    rise = tremolo(tone(240, 0.42, sine, amp=0.26, attack=0.004, release=0.12, freq_end=920), 12.0, depth=0.5)
    edge = tone(480, 0.42, square, amp=0.05, attack=0.004, release=0.12, freq_end=1840)
    return mix(rise, edge)


def gen_sweep():
    """The green bar crossing the bottom rows: a whoosh, then three pops."""
    whoosh = concat(tone(200, 0.13, sine, amp=0.30, attack=0.004, release=0.01, freq_end=1200),
                    tone(1200, 0.12, sine, amp=0.28, attack=0.001, release=0.06, freq_end=320))
    air = noise(0.25, amp=0.10, attack=0.04, release=0.12, seed=29)
    pops = concat(tone(500, 0.03, square, amp=0.18, attack=0.001, release=0.02), silence(0.01),
                  tone(600, 0.03, square, amp=0.18, attack=0.001, release=0.02), silence(0.01),
                  tone(720, 0.05, square, amp=0.18, attack=0.001, release=0.04))
    return concat(mix(whoosh, air), pops)


def gen_b2b():
    """Back to back: a sparkle, four quick high notes and a ring."""
    notes = [2093, 2637, 3136, 4186]
    parts = [tone(f, 0.035, sine, amp=0.18, attack=0.002, release=0.02) for f in notes]
    parts.append(tone(3136, 0.14, sine, amp=0.16, attack=0.002, release=0.12))
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
        # 3.1.0
        "lock.wav": gen_lock(),
        "hold.wav": gen_hold(),
        "bomb.wav": gen_bomb(),
        "laser.wav": gen_laser(),
        "freeze.wav": gen_freeze(),
        "thaw.wav": gen_thaw(),
        "slow.wav": gen_slow(),
        "windup.wav": gen_windup(),
        "sweep.wav": gen_sweep(),
        "b2b.wav": gen_b2b(),
    }
    for name, samples in sounds.items():
        path = os.path.join(out_dir, name)
        write_wav(path, samples)
        print(f"wrote {path} ({len(samples)/SR:.2f}s)")


if __name__ == "__main__":
    main()
