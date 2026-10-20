"""Checks that the channel 5 photodiode board behaves like a linear TIA.

Dark offset and noise, a fine level sweep against the board's own current
readback (linearity, zero intercept, where it clips), stability during a
long pulse, and pulse-to-pulse repeatability. Levels stay low (<= 0.18,
about 1.3 A) and every pulse is timed by the firmware.

Run from the repo root:  .venv/bin/python scratch/photodiode_check.py [Rf_ohms]
"""

import statistics as st
import sys
import time

from syncboard import FeedbackMode, SyncBoard

CH = 5
RF = float(sys.argv[1]) if len(sys.argv) > 1 else 10e3
ADC_CLIP_MV = 2990.0           # (3.3 V full scale - 0.3 V offset) less a margin


def reads(sb, n, gap=0.0):
    out = []
    for _ in range(n):
        out.append(sb.leds.measure_photodiode(CH))
        if gap:
            time.sleep(gap)
    return out


def fit(xs, ys):
    """Least-squares y = a + b x; returns a, b, max |residual|, r^2."""
    mx, my = st.fmean(xs), st.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    a = my - b * mx
    res = [y - (a + b * x) for x, y in zip(xs, ys)]
    ss = sum((y - my) ** 2 for y in ys)
    return a, b, max(abs(r) for r in res), 1 - sum(r * r for r in res) / ss


with SyncBoard.connect() as sb:
    sb.initialise(led_board=True)

    # 1. Dark
    dark = reads(sb, 100)
    print(f"dark: mean {st.fmean(dark):.2f} mV, sd {st.pstdev(dark):.2f} mV, "
          f"min {min(dark):.2f}, max {max(dark):.2f}  (1 ADC LSB = 0.806 mV)")

    # 2. Fine sweep against measured current
    rows = []
    levels = [round(0.01 * i, 2) for i in range(1, 19)]
    for lv in levels:
        sb.leds.set_level(CH, lv, FeedbackMode.CURRENT)
        sb.leds.pulse(CH, 600)
        time.sleep(0.1)
        m = sb.leds.measure(CH)              # ~100 ms, during the pulse
        pd = reads(sb, 20, 0.005)            # also during the pulse
        time.sleep(0.6)                      # pulse over
        after = sb.leds.measure_photodiode(CH)
        rows.append((lv, m.current_a, st.fmean(pd), st.pstdev(pd), after))
        time.sleep(0.3)
    print("\nlevel  I_meas(A)  PD(mV)   sd(mV)  PD/I(mV/A)  Iph(uA)  after(mV)")
    for lv, i, pd, sd, after in rows:
        print(f"{lv:5.2f}  {i:8.4f}  {pd:8.1f}  {sd:6.2f}  {pd / i:9.1f}  "
              f"{pd / (2 * RF) * 1e3:7.2f}  {after:8.2f}")
    lin = [(i, pd) for lv, i, pd, sd, after in rows if pd < ADC_CLIP_MV]
    a, b, maxres, r2 = fit(*zip(*lin))
    print(f"\nfit over {len(lin)} unclipped points: PD = {a:.1f} mV + {b:.1f} mV/A x I"
          f"  (max residual {maxres:.1f} mV, r^2 {r2:.6f})")
    lo = [(i, pd) for i, pd in lin if pd < 1500]
    hi = [(i, pd) for i, pd in lin if pd >= 1500]
    if len(lo) >= 2 and len(hi) >= 2:
        print(f"slope below 1.5 V: {fit(*zip(*lo))[1]:.1f} mV/A, "
              f"above: {fit(*zip(*hi))[1]:.1f} mV/A (compression shows as a lower slope)")

    # 3. Stability during one long pulse
    sb.leds.set_level(CH, 0.1, FeedbackMode.CURRENT)
    sb.leds.pulse(CH, 3000)
    t0 = time.monotonic()
    trace = []
    while time.monotonic() - t0 < 2.9:
        trace.append((time.monotonic() - t0, sb.leds.measure_photodiode(CH)))
        time.sleep(0.05)
    time.sleep(0.3)
    early = [v for t, v in trace if 0.05 < t < 0.3]
    late = [v for t, v in trace if t > 2.5]
    print(f"\n3 s pulse at 0.1: first {trace[0][1]:.1f} mV at {trace[0][0]*1e3:.0f} ms, "
          f"0.05-0.3 s mean {st.fmean(early):.1f}, 2.5-2.9 s mean {st.fmean(late):.1f} "
          f"({(st.fmean(late) / st.fmean(early) - 1) * 100:+.2f} %), "
          f"sd over pulse {st.pstdev([v for t, v in trace if t > 0.05]):.2f} mV")

    # 4. Repeatability
    reps = []
    for _ in range(8):
        sb.leds.pulse(CH, 300)
        time.sleep(0.1)
        reps.append(st.fmean(reads(sb, 10)))
        time.sleep(0.5)
    print(f"8 pulses at 0.1: mean {st.fmean(reps):.1f} mV, sd {st.pstdev(reps):.2f} mV, "
          f"range {max(reps) - min(reps):.1f} mV")

    dark2 = reads(sb, 50)
    print(f"dark after: mean {st.fmean(dark2):.2f} mV, sd {st.pstdev(dark2):.2f} mV")
