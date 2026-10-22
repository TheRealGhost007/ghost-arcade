#!/usr/bin/env python3
"""Generates the sound effects for Pond Kings as 16-bit mono PCM WAV files
(44100 Hz) in assets/sounds/.

Pond Kings is pond hockey on a winter night, so these are meant to sound like
wood and rubber, snow and ice, a sleigh bell, a pea whistle and an old pickup
truck's horn -- not arcade bleeps. Every sound is synthesized from scratch
(filtered noise, decaying partials, band-limited oscillators); nothing is
sampled or downloaded. Uses only the Python standard library, and all the
randomness is seeded (each sound has its own stream), so every run writes
byte-identical files and changing one sound never changes another.

Each sound is peak-normalised to -3 dBFS, so how loud one feels next to
another comes from its body and length, not its peak.
"""
import math
import os
import random
import struct
import wave

SR = 44100
SEED = 333                      # three on three
PEAK = 10.0 ** (-3.0 / 20.0)    # -3 dBFS
TWO_PI = 2.0 * math.pi


# ------------------------------------------------------------ building blocks

def length(seconds):
    return int(round(seconds * SR))


def silence(seconds):
    return [0.0] * length(seconds)


def mix_into(dst, src, at=0.0, gain=1.0):
    """Adds src into dst starting `at` seconds in; whatever runs past the end is dropped."""
    start = length(at)
    for i in range(min(len(src), len(dst) - start)):
        dst[start + i] += src[i] * gain
    return dst


def scaled(x, peak):
    """x rescaled so its loudest sample is `peak`: sets a layer's level before mixing."""
    top = max(abs(v) for v in x)
    return [v * peak / top for v in x] if top > 0.0 else x


def times(x, env):
    return [a * b for a, b in zip(x, env)]


def strike(n, attack, decay):
    """Envelope of a hit: a short linear rise, then an exponential fall (time constant `decay` s)."""
    a = max(1, length(attack))
    k = math.exp(-1.0 / (decay * SR))
    out, level = [0.0] * n, 1.0
    for i in range(n):
        if i < a:
            out[i] = (i + 1) / a
        else:
            level *= k
            out[i] = level
    return out


def shape(n, points):
    """Piecewise-linear envelope through (seconds, level) points; holds the last level."""
    out = [points[-1][1]] * n
    for (t0, v0), (t1, v1) in zip(points, points[1:]):
        i0, i1 = length(t0), min(n, length(t1))
        for i in range(i0, i1):
            out[i] = v0 + (v1 - v0) * (i - i0) / max(1, length(t1) - i0)
    return out


def wander(n, rng, step, low, high):
    """A slow random drift between low and high, heading somewhere new every `step` s."""
    points, t = [], 0.0
    while t < n / SR + step:
        points.append((t, rng.uniform(low, high)))
        t += step
    return shape(n, points)


def glide(n, f0, f1, seconds=None):
    """Per-sample frequency sliding exponentially from f0 to f1 over `seconds`
    (default: the whole length), then holding."""
    m = max(1, length(seconds) if seconds else n)
    return [f0 * (f1 / f0) ** min(1.0, i / m) for i in range(n)]


def noise(n, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def burst(n, rng, attack, decay):
    """A noise transient: the contact click of a hit."""
    return times(noise(n, rng), strike(n, attack, decay))


def grains(n, rng, count, start, span, size=(0.0004, 0.0015), bunch=2.0, fade=0.75):
    """Snow crunch, ice chips, a rattling net: `count` tiny noise grains between
    `start` and `start + span` seconds. `bunch` crowds them toward the start and
    `fade` makes the later ones quieter."""
    out = [0.0] * n
    for _ in range(count):
        u = rng.random() ** bunch
        i0 = length(start + u * span)
        g = max(2, length(rng.uniform(*size)))
        amp = rng.uniform(0.35, 1.0) * (1.0 - fade * u)
        for j in range(min(g, n - i0)):
            out[i0 + j] += rng.uniform(-1.0, 1.0) * amp * math.sin(math.pi * (j + 0.5) / g)
    return out


def chatter(n, rng, rate, depth):
    """A rough, jittery flutter at about `rate` Hz to multiply a scrape by:
    a blade skittering over the ice."""
    out, i = [1.0] * n, 0
    while i < n:
        period = max(2, length(1.0 / (rate * rng.uniform(0.7, 1.3))))
        bump = rng.uniform(0.5, 1.0)
        for j in range(min(period, n - i)):
            out[i + j] = 1.0 - depth + depth * bump * math.sin(math.pi * j / period) ** 2
        i += period
    return out


# ---------------------------------------------------------------- oscillators

def sine(freqs, phase=0.0):
    out = [0.0] * len(freqs)
    for i, f in enumerate(freqs):
        out[i] = math.sin(TWO_PI * phase)
        phase += f / SR
        if phase >= 1.0:
            phase -= 1.0
    return out


def _blep(t, dt):
    """PolyBLEP: rounds off a waveform's jump so it doesn't alias."""
    if t < dt:
        t /= dt
        return t + t - t * t - 1.0
    if t > 1.0 - dt:
        t = (t - 1.0) / dt
        return t * t + t + t + 1.0
    return 0.0


def saw(freqs, phase=0.0):
    out = [0.0] * len(freqs)
    for i, f in enumerate(freqs):
        dt = f / SR
        out[i] = 2.0 * phase - 1.0 - _blep(phase, dt)
        phase += dt
        if phase >= 1.0:
            phase -= 1.0
    return out


def pulse(freqs, duty=0.5, phase=0.0):
    out = [0.0] * len(freqs)
    for i, f in enumerate(freqs):
        dt = f / SR
        v = 1.0 if phase < duty else -1.0
        v += _blep(phase, dt)
        v -= _blep((phase + 1.0 - duty) % 1.0, dt)
        out[i] = v
        phase += dt
        if phase >= 1.0:
            phase -= 1.0
    return out


def partials(n, modes, attack=0.0004):
    """A struck body ringing: decaying sine partials, each (Hz, amplitude, decay
    time constant in s). Short decays sound like wood, long ones like metal."""
    out = [0.0] * n
    a = max(1, length(attack))
    for f, amp, decay in modes:
        w = TWO_PI * f / SR
        k = math.exp(-1.0 / (decay * SR))
        level = amp
        for i in range(n):
            out[i] += level * math.sin(w * i) * min(1.0, (i + 1) / a)
            level *= k
            if level < amp * 1e-4:
                break
    return out


def thud(n, f0, f1, sweep, decay, drive=2.0):
    """A low body blow: a sine falling in pitch, rounded off by a little
    saturation so its overtones still carry on small speakers."""
    body = times(sine(glide(n, f0, f1, sweep)), strike(n, 0.002, decay))
    return soft_clip(body, drive)


# -------------------------------------------------------------------- filters

def svf(x, cutoff, q=0.707, mode="lp"):
    """State-variable filter (the trapezoidal "TPT" form, which stays stable
    while it sweeps). `cutoff` is in Hz, a number or a per-sample list; mode is
    lp, hp or bp (unity gain at the centre)."""
    k = 1.0 / q
    sweep = isinstance(cutoff, list)
    ic1 = ic2 = 0.0
    out = [0.0] * len(x)
    a1 = a2 = a3 = 0.0
    if not sweep:
        g = math.tan(math.pi * min(cutoff, 0.45 * SR) / SR)
        a1 = 1.0 / (1.0 + g * (g + k))
        a2, a3 = g * a1, g * g * a1
    for i, v0 in enumerate(x):
        if sweep:
            g = math.tan(math.pi * min(cutoff[i], 0.45 * SR) / SR)
            a1 = 1.0 / (1.0 + g * (g + k))
            a2, a3 = g * a1, g * g * a1
        v3 = v0 - ic2
        v1 = a1 * ic1 + a2 * v3
        v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2.0 * v1 - ic1
        ic2 = 2.0 * v2 - ic2
        if mode == "lp":
            out[i] = v2
        elif mode == "bp":
            out[i] = k * v1
        else:
            out[i] = v0 - k * v1 - v2
    return out


def lowpass(x, cutoff):
    """Gentle one-pole low-pass (6 dB per octave)."""
    a = 1.0 - math.exp(-TWO_PI * cutoff / SR)
    y, out = 0.0, [0.0] * len(x)
    for i, v in enumerate(x):
        y += a * (v - y)
        out[i] = y
    return out


def highpass(x, cutoff):
    return [v - lp for v, lp in zip(x, lowpass(x, cutoff))]


def soft_clip(x, drive):
    norm = math.tanh(drive)
    return [math.tanh(drive * v) / norm for v in x]


def slapback(x, delay, gain, cutoff):
    """One soft echo, duller than the sound itself: off the houses across the pond."""
    wet = lowpass([0.0] * length(delay) + x, cutoff)
    return [v + gain * w for v, w in zip(x, wet)]


def finish(x, fade):
    """Removes any DC, fades the last `fade` seconds to silence and
    peak-normalises to -3 dBFS."""
    x = highpass(x, 18.0)
    n = len(x)
    f = min(n, max(1, length(fade)))
    for i in range(f):
        x[n - 1 - i] *= i / f
    return scaled(x, PEAK)


def write_wav(path, x):
    pcm = struct.pack("<%dh" % len(x), *(max(-32767, min(32767, int(round(v * 32767)))) for v in x))
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SR)
        f.writeframes(pcm)


# ------------------------------------------------------------ shared sources

def sleigh_bell(rng, f0, seconds):
    """One shake of a small sleigh bell: the pellet rattles, so a few quick,
    weakening strikes, each ringing the bell and a slightly sharper neighbour
    on the same strap, with a glassy tick of noise."""
    n = length(seconds)
    out = [0.0] * n
    at, amp = 0.0, 1.0
    for _ in range(4):
        m = n - length(at)
        for detune, gain in ((1.0, 1.0), (1.013, 0.5)):
            f = f0 * detune * rng.uniform(0.998, 1.002)
            ring = partials(m, [(f, 1.0, 0.13), (f * 2.01, 0.38, 0.07), (f * 2.76, 0.30, 0.05),
                                (f * 4.07, 0.16, 0.03), (f * 5.43, 0.08, 0.02)], attack=0.0003)
            mix_into(out, ring, at, amp * gain)
        mix_into(out, scaled(highpass(burst(m, rng, 0.0001, 0.0008), 5000), 0.25), at, amp)
        at += rng.uniform(0.012, 0.022)
        amp *= rng.uniform(0.42, 0.55)
    return out


def tock(rng, n, pitch=1.0):
    """The puck landing flat on the ice: a small hard-rubber "tock" with a tick of ice."""
    body = partials(n, [(735 * pitch, 1.0, 0.013), (1560 * pitch, 0.45, 0.008),
                        (2700 * pitch, 0.2, 0.005), (5200 * pitch, 0.12, 0.003)])
    out = [0.0] * n
    mix_into(out, scaled(body, 1.0))
    mix_into(out, scaled(lowpass(burst(n, rng, 0.0001, 0.0005), 6000), 0.25))
    return out


def clack(rng, n, f, gain):
    """Stick on stick: a bright, quickly damped knock of hard wood."""
    body = partials(n, [(f, 1.0, 0.016), (f * 2.19, 0.6, 0.010), (f * 3.56, 0.33, 0.006)])
    out = [0.0] * n
    mix_into(out, scaled(body, gain))
    mix_into(out, scaled(highpass(burst(n, rng, 0.0001, 0.0007), 1500), 0.4 * gain))
    return out


def babble(rng, n, voices=6):
    """A crowd of kids, none of them saying anything in particular: whispery
    noise through a pair of vowel formants that keep moving, each voice rising
    and falling syllable by syllable."""
    out = [0.0] * n
    for _ in range(voices):
        air = noise(n, rng)
        f1 = wander(n, rng, 0.09, 600.0, 1000.0)
        f2 = wander(n, rng, 0.09, 1700.0, 2900.0)
        voice = [a + 0.6 * b for a, b in zip(svf(air, f1, 3.0, "bp"), svf(air, f2, 4.0, "bp"))]
        mix_into(out, times(voice, wander(n, rng, rng.uniform(0.07, 0.12), 0.1, 1.0)))
    return lowpass(out, 5000)


def yay(rng, f0, seconds):
    """One kid's "yaaay!": a bright, buzzy voice through moving formants
    (ee -> ah -> ay), its pitch leaping up then falling away, with breath and wobble."""
    n = length(seconds)
    contour = shape(n, [(0.0, 0.9), (seconds * 0.3, 1.18), (seconds, 0.82)])
    rate = rng.uniform(5.0, 7.0)
    freqs = [f0 * c * (1.0 + 0.018 * math.sin(TWO_PI * rate * i / SR)) for i, c in enumerate(contour)]
    source = [v + 0.15 * w for v, w in zip(saw(freqs, rng.random()), noise(n, rng))]
    f1 = shape(n, [(0.0, 420.0), (seconds * 0.25, 880.0), (seconds, 690.0)])
    f2 = shape(n, [(0.0, 3000.0), (seconds * 0.25, 2300.0), (seconds, 2650.0)])
    voice = [a + 0.55 * b + 0.25 * c for a, b, c in zip(svf(source, f1, 4.0, "bp"),
                                                         svf(source, f2, 6.0, "bp"),
                                                         svf(source, 3700.0, 7.0, "bp"))]
    return times(voice, shape(n, [(0.0, 0.0), (0.05, 1.0), (seconds - 0.18, 0.85), (seconds, 0.0)]))


# ---------------------------------------------------------------------- menus

def gen_select(rng):
    # Menu move: a soft wooden tick.
    n = length(0.07)
    out = silence(0.07)
    mix_into(out, scaled(partials(n, [(1500, 1.0, 0.008), (2480, 0.42, 0.005), (3800, 0.16, 0.003)]), 1.0))
    mix_into(out, scaled(lowpass(burst(n, rng, 0.0002, 0.0007), 5000), 0.4))
    return lowpass(out, 6500)


def gen_confirm(rng):
    # Menu confirm: two quick shakes of a small sleigh bell, the second a fourth higher.
    out = silence(0.5)
    mix_into(out, sleigh_bell(rng, 1318.5, 0.5), 0.0, 0.75)     # E6
    mix_into(out, sleigh_bell(rng, 1760.0, 0.405), 0.095, 1.0)  # A6
    return out


def gen_back(rng):
    # Menu back: a lower knock on wood, a knuckle on a fence post.
    n = length(0.14)
    out = silence(0.14)
    body = partials(n, [(205, 0.4, 0.020), (520, 1.0, 0.026), (1230, 0.45, 0.012), (2060, 0.2, 0.007)])
    mix_into(out, scaled(body, 1.0))
    mix_into(out, scaled(lowpass(burst(n, rng, 0.0003, 0.0015), 2500), 0.35))
    return lowpass(out, 4000)


# ------------------------------------------------------------------ the puck

def gen_pass(rng):
    # A pass: a short woody click of stick on puck, and the blade rings a little.
    n = length(0.2)
    out = silence(0.2)
    mix_into(out, scaled(partials(n, [(1380, 1.0, 0.009), (2620, 0.55, 0.0055), (3950, 0.25, 0.0035)]), 1.0))
    mix_into(out, scaled(highpass(burst(n, rng, 0.0001, 0.0005), 2000), 0.45))
    mix_into(out, scaled(partials(n, [(1040, 1.0, 0.05), (3170, 0.3, 0.028)]), 0.3))
    mix_into(out, scaled(partials(n, [(270, 1.0, 0.012)]), 0.35))
    return out


def gen_shot(rng):
    # A wrist shot: a sharper, louder snap of stick on puck (more crack and
    # less click than a pass), then the puck hisses away.
    n = length(0.38)
    out = silence(0.38)
    crack = partials(n, [(780, 1.0, 0.022), (1690, 0.8, 0.015), (3150, 0.5, 0.008), (4800, 0.25, 0.0045)])
    mix_into(out, scaled(crack, 1.0))
    mix_into(out, scaled(highpass(burst(n, rng, 0.0001, 0.0012), 900), 0.55))
    mix_into(out, scaled(thud(n, 170, 95, 0.05, 0.02), 0.25))
    m = length(0.33)
    whoosh = svf(noise(m, rng), glide(m, 3000, 900), 1.8, "bp")
    whoosh = times(whoosh, shape(m, [(0.0, 0.0), (0.02, 1.0), (0.08, 0.6), (0.18, 0.25), (0.33, 0.0)]))
    mix_into(out, scaled(whoosh, 0.5), 0.006)
    return out


def gen_onetimer(rng):
    # A one-timer: the blade slaps the ice, a big crack, and a long whoosh.
    n = length(0.56)
    out = silence(0.56)
    mix_into(out, scaled(svf(burst(n, rng, 0.0004, 0.004), 2800, 0.9, "bp"), 0.3))
    mix_into(out, scaled(thud(n, 120, 90, 0.03, 0.012), 0.25))
    hit = 0.022
    crack = partials(n, [(640, 1.0, 0.030), (1420, 0.8, 0.020), (2750, 0.55, 0.012), (4400, 0.3, 0.007)])
    mix_into(out, scaled(crack, 1.0), hit)
    mix_into(out, scaled(highpass(burst(n, rng, 0.0001, 0.002), 600), 0.6), hit)
    mix_into(out, scaled(thud(n, 150, 80, 0.08, 0.035), 0.4), hit)
    m = length(0.5)
    air = noise(m, rng)
    env = shape(m, [(0.0, 0.0), (0.03, 1.0), (0.12, 0.7), (0.3, 0.3), (0.5, 0.0)])
    wobble = [1.0 + 0.15 * math.sin(TWO_PI * 17.0 * i / SR) for i in range(m)]
    whoosh = times(times(svf(air, glide(m, 3400, 650), 1.6, "bp"), env), wobble)
    mix_into(out, scaled(whoosh, 0.6), hit + 0.008)
    mix_into(out, scaled(times(svf(air, glide(m, 1300, 320), 0.8, "lp"), env), 0.22), hit + 0.008)
    return out


def gen_post(rng):
    # Off the post: a clear ping from the steel pipe, ringing out. The two
    # nearly equal lowest partials beat slowly, the way a real pipe shimmers.
    n = length(0.8)
    out = silence(0.8)
    f = 1620.0
    ring = partials(n, [(f, 0.62, 0.30), (f * 1.0045, 0.5, 0.27), (f * 2.756, 0.45, 0.15),
                        (f * 5.404, 0.2, 0.06), (f * 8.933, 0.07, 0.025)], attack=0.0002)
    mix_into(out, scaled(ring, 1.0))
    mix_into(out, scaled(highpass(burst(n, rng, 0.0001, 0.0004), 3000), 0.35))
    mix_into(out, scaled(partials(n, [(420, 1.0, 0.007)]), 0.22))
    return out


def gen_net(rng):
    # Into the mesh, or off a body: a soft thump and a little rattle of the net.
    n = length(0.35)
    out = silence(0.35)
    mix_into(out, scaled(thud(n, 130, 72, 0.08, 0.035), 1.0))
    mix_into(out, scaled(lowpass(lowpass(burst(n, rng, 0.001, 0.02), 900), 900), 0.5))
    rattle = svf(grains(n, rng, 26, 0.004, 0.22, size=(0.0006, 0.0025)), 2900, 1.2, "bp")
    mix_into(out, scaled(rattle, 0.32))
    return out


def gen_goal(rng):
    # The goal horn: an old pickup truck's horn, honk-honk. Two buzzy tones a
    # slightly flat major third apart, each honk starting a little low as the
    # diaphragm gets going and sagging as it lets go, through a soft low-pass
    # with a nasal bump, and one faint echo off the houses.
    n = length(0.98)
    gate, bend = [0.0] * n, [1.0] * n
    for start, hold in ((0.0, 0.22), (0.31, 0.47)):
        i0, m = length(start), length(hold)
        env = shape(m, [(0.0, 0.0), (0.008, 1.0), (hold - 0.03, 0.92), (hold, 0.0)])
        for j in range(m):
            t = j / SR
            gate[i0 + j] = env[j]
            bend[i0 + j] = 1.0 - 0.05 * math.exp(-t / 0.014) - 0.012 * max(0.0, (t - hold + 0.05) / 0.05)
    wobble = [b * (1.0 + 0.003 * math.sin(TWO_PI * 5.5 * i / SR)) for i, b in enumerate(bend)]
    low = saw([345.0 * w for w in wobble])
    high = pulse([428.0 * w for w in wobble], duty=0.42)
    buzz = soft_clip([(a + 0.8 * b) * g * 0.6 for a, b, g in zip(low, high, gate)], 1.8)
    horn = [a + 0.55 * b for a, b in zip(svf(buzz, 1700, 0.8, "lp"), svf(buzz, 1050, 2.5, "bp"))]
    return slapback(highpass(horn, 140), 0.14, 0.16, 1400)


def gen_cheer(rng):
    # Kids cheering after a goal: a babble of voices that swells and fades,
    # with a few of them gliding up and down -- "yaaay!".
    n = length(1.2)
    out = silence(1.2)
    swell = shape(n, [(0.0, 0.0), (0.1, 0.4), (0.3, 1.0), (0.55, 0.85), (0.85, 0.4), (1.2, 0.0)])
    mix_into(out, scaled(times(babble(rng, n), swell), 1.0))
    for _ in range(5):
        start = rng.uniform(0.02, 0.38)
        hold = rng.uniform(0.42, 0.7)
        mix_into(out, scaled(yay(rng, rng.uniform(340, 560), hold), rng.uniform(0.25, 0.4)), start)
    return out


# ------------------------------------------------------------- snow and ice

def gen_bank(rng):
    # A hard puck buries itself in a snowbank: a muffled low "pff" thud.
    n = length(0.3)
    out = silence(0.3)
    mix_into(out, scaled(svf(burst(n, rng, 0.003, 0.07), glide(n, 1800, 240, 0.18), 0.6, "lp"), 1.0))
    mix_into(out, scaled(thud(n, 130, 68, 0.14, 0.05), 0.9))
    crunch = svf(grains(n, rng, 30, 0.0, 0.09, size=(0.0003, 0.001)), 1600, 0.8, "bp")
    mix_into(out, scaled(lowpass(crunch, 2500), 0.18))
    return out


def gen_bump(rng):
    # A slow puck nudges a snowbank: a softer, shorter snow thud.
    n = length(0.16)
    out = silence(0.16)
    mix_into(out, scaled(svf(burst(n, rng, 0.004, 0.02), glide(n, 800, 180, 0.08), 0.6, "lp"), 0.7))
    mix_into(out, scaled(thud(n, 140, 90, 0.07, 0.018, drive=1.5), 1.0))
    crunch = svf(grains(n, rng, 10, 0.0, 0.05, size=(0.0003, 0.0009)), 1300, 0.8, "bp")
    mix_into(out, scaled(lowpass(crunch, 2000), 0.1))
    return out


def gen_dig(rng):
    # A kid digs the puck out with a shovel: a short scrape, a flick that throws
    # it free in a spray of snow, and it lands back on the ice.
    out = silence(0.46)
    m = length(0.22)
    scrape = svf(noise(m, rng), glide(m, 1700, 2500), 2.2, "bp")
    scrape = times(times(scrape, chatter(m, rng, 90, 0.5)),
                   shape(m, [(0.0, 0.0), (0.02, 0.8), (0.17, 1.0), (0.22, 0.0)]))
    mix_into(out, scaled(scrape, 0.8))
    crunch = svf(grains(m, rng, 40, 0.0, 0.2, bunch=1.0, fade=0.0), 2200, 1.0, "bp")
    mix_into(out, scaled(crunch, 0.3))
    k = length(0.12)
    flick = svf(noise(k, rng), glide(k, 1400, 4200, 0.07), 1.4, "bp")
    mix_into(out, scaled(times(flick, shape(k, [(0.0, 0.0), (0.012, 1.0), (0.08, 0.0)])), 0.6), 0.23)
    mix_into(out, scaled(highpass(grains(k, rng, 25, 0.01, 0.1), 3500), 0.2), 0.23)
    mix_into(out, scaled(tock(rng, length(0.09)), 0.45), 0.36)
    return out


def gen_check(rng):
    # A body check: a low, padded thud and a little crunch of snow and skates.
    n = length(0.3)
    out = silence(0.3)
    mix_into(out, scaled(thud(n, 120, 60, 0.12, 0.05), 1.0))
    mix_into(out, scaled(partials(n, [(230, 1.0, 0.025)]), 0.4))
    mix_into(out, scaled(lowpass(lowpass(burst(n, rng, 0.0015, 0.028), 900), 900), 0.6))
    crunch = svf(grains(n, rng, 36, 0.002, 0.08, size=(0.0003, 0.0012)), 2400, 0.9, "bp")
    mix_into(out, scaled(crunch, 0.3))
    scritch = highpass(times(noise(n, rng), shape(n, [(0.0, 0.0), (0.012, 1.0), (0.05, 0.0)])), 4500)
    mix_into(out, scaled(scritch, 0.1), 0.008)
    return out


def gen_poke(rng):
    # A poke check: stick clacks on stick, and clatters once more.
    out = silence(0.14)
    mix_into(out, clack(rng, length(0.14), 990, 1.0))
    mix_into(out, clack(rng, length(0.113), 1060, 0.45), 0.027)
    return out


def gen_stop(rng):
    # A hard hockey stop: the blades shave the ice, a bright hiss of ice chips.
    n = length(0.3)
    out = silence(0.3)
    air = noise(n, rng)
    hiss = [0.6 * a + 0.8 * b for a, b in zip(svf(air, 2600, 0.7, "hp"),
                                              svf(air, glide(n, 6200, 4300, 0.28), 0.9, "bp"))]
    hiss = times(times(hiss, chatter(n, rng, 100, 0.55)),
                 shape(n, [(0.0, 0.0), (0.01, 1.0), (0.05, 0.65), (0.14, 0.35), (0.3, 0.0)]))
    mix_into(out, scaled(hiss, 0.8))
    mix_into(out, scaled(svf(grains(n, rng, 30, 0.0, 0.035), 3000, 1.0, "bp"), 0.5))
    return out


def gen_drop(rng):
    # The faceoff: the puck drops onto the ice, "tock", with one tiny bounce.
    out = silence(0.14)
    mix_into(out, tock(rng, length(0.14)))
    mix_into(out, tock(rng, length(0.088), pitch=1.03), 0.052, 0.28)
    return out


def gen_whistle(rng):
    # End of the period: a pea whistle. A pure tone near 2.6 kHz with the fast
    # warble of the pea spinning inside it, and a little breath.
    n = length(0.7)
    tone = [0.0] * n
    phase = trill = 0.0
    for i in range(n):
        t = i / SR
        trill += 31.0 * (1.0 + 0.06 * math.sin(TWO_PI * 2.7 * t)) / SR
        warble = math.sin(TWO_PI * trill)
        f = 2600.0 * (1.0 - 0.07 * math.exp(-t / 0.012)) + 105.0 * warble
        phase += f / SR
        phase -= math.floor(phase)
        p = TWO_PI * phase
        level = 1.0 - 0.5 * (0.5 + 0.5 * math.sin(TWO_PI * trill + 0.6))
        tone[i] = (math.sin(p) + 0.08 * math.sin(2.0 * p) + 0.025 * math.sin(3.0 * p)) * level
    out = silence(0.7)
    mix_into(out, scaled(tone, 1.0))
    mix_into(out, scaled(svf(noise(n, rng), 2650, 3.0, "bp"), 0.08))
    mix_into(out, scaled(highpass(noise(n, rng), 3000), 0.04))
    return times(out, shape(n, [(0.0, 0.0), (0.02, 1.0), (0.6, 0.95), (0.7, 0.0)]))


def gen_shovel(rng):
    # Between periods somebody shovels the ice: three scraping pushes.
    out = silence(0.9)
    for start in (0.0, 0.3, 0.6):
        m = length(0.26)
        air = noise(m, rng)
        tilt = rng.uniform(0.92, 1.08)
        scrape = svf(air, glide(m, 1250 * tilt, 2100 * tilt), 1.8, "bp")
        stroke = [a + 0.35 * b for a, b in zip(scrape, svf(air, 3500, 0.7, "hp"))]
        stroke = times(times(stroke, chatter(m, rng, 80, 0.45)),
                       shape(m, [(0.0, 0.0), (0.03, 0.55), (0.18, 1.0), (0.215, 0.9), (0.25, 0.0)]))
        mix_into(out, scaled(stroke, rng.uniform(0.85, 1.0)), start)
    return out


def gen_false(rng):
    # A false start at the faceoff: a dull, low "bonk", a knock on a hollow log.
    n = length(0.26)
    out = silence(0.26)
    mix_into(out, scaled(thud(n, 200, 150, 0.12, 0.035, drive=1.2), 1.0))
    mix_into(out, scaled(partials(n, [(470, 1.0, 0.024), (780, 0.35, 0.010)]), 0.35))
    mix_into(out, scaled(lowpass(burst(n, rng, 0.0002, 0.001), 1500), 0.2))
    return lowpass(out, 2200)


# The C code loads exactly these files. (name, generator, tail fade in s)
SOUNDS = [
    ("select", gen_select, 0.006),
    ("confirm", gen_confirm, 0.05),
    ("back", gen_back, 0.01),
    ("pass", gen_pass, 0.02),
    ("shot", gen_shot, 0.02),
    ("onetimer", gen_onetimer, 0.03),
    ("post", gen_post, 0.2),
    ("net", gen_net, 0.03),
    ("goal", gen_goal, 0.03),
    ("cheer", gen_cheer, 0.05),
    ("bank", gen_bank, 0.03),
    ("bump", gen_bump, 0.02),
    ("dig", gen_dig, 0.02),
    ("check", gen_check, 0.03),
    ("poke", gen_poke, 0.01),
    ("stop", gen_stop, 0.02),
    ("drop", gen_drop, 0.01),
    ("whistle", gen_whistle, 0.01),
    ("shovel", gen_shovel, 0.02),
    ("false", gen_false, 0.02),
]


# Long, steady tones sound far louder than a click at the same peak, and the
# sound module has one volume for everything, so these are trimmed here (dB).
TRIM_DB = {"whistle": -9.0, "goal": -5.0, "shovel": -5.0, "post": -3.0}


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "sounds")
    os.makedirs(out_dir, exist_ok=True)
    for name, gen, fade in SOUNDS:
        samples = finish(gen(random.Random("%d:%s" % (SEED, name))), fade)
        gain = 10.0 ** (TRIM_DB.get(name, 0.0) / 20.0)
        samples = [v * gain for v in samples]
        write_wav(os.path.join(out_dir, name + ".wav"), samples)
        print("  %-13s %.3f s" % (name + ".wav", len(samples) / SR))
    print("wrote %d sounds to %s" % (len(SOUNDS), os.path.normpath(out_dir)))


if __name__ == "__main__":
    main()
