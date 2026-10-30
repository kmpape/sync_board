"""Minimal LED on/off GUI for optics alignment (what optics_alignment.py does).

Run with ``uv run python scratch/optics_alignment_gui.py``. Closing the
window turns the LEDs off, disables the system and closes the port.

"On", "Pulse", "Hold" and "Calibrate" enable the system first if it is
disabled; "Disable" puts every output into a safe state until the next of
those.

"Hold" keeps the LED on at any level by re-issuing a timed pulse every 10 s
(the firmware refuses an untimed "On" above 30 % of the calibrated range,
and a timed pulse above 10 s). Expect a ~100 ms blink at each re-issue.
"Off" stops it.

Firmware warnings (e.g. a calibration truncated by the over-current trip)
are shown in the status line as well as on the terminal.
"""
import logging
import tkinter as tk
from tkinter import ttk

from syncboard import FeedbackMode, SyncBoard


def main():
    with SyncBoard.connect() as sb:  # auto-discovers the port
        sb.initialise(led_board=True)

        root = tk.Tk()
        root.title("LED alignment")
        ch = tk.IntVar(value=5)
        level = tk.DoubleVar(value=0.05)
        feedback = tk.StringVar(value="CURRENT")
        pulse_ms = tk.DoubleVar(value=100)
        max_current = tk.DoubleVar(value=5.0)
        status = tk.StringVar()
        system = tk.StringVar()
        hold_job = [None]  # pending root.after() id while "Hold" is active

        class StatusHandler(logging.Handler):
            def emit(self, record):
                status.set(record.getMessage())

        logging.basicConfig(level=logging.WARNING)  # firmware warnings on the terminal
        logging.getLogger("syncboard").addHandler(StatusHandler(logging.WARNING))

        def show_system():
            system.set("System: " + ("enabled" if sb.status().enabled else "DISABLED"))

        def ensure_enabled():
            if not sb.status().enabled:
                sb.enable()

        def stop_hold():
            if hold_job[0] is not None:
                root.after_cancel(hold_job[0])
                hold_job[0] = None

        HOLD_PULSE_MS = 9900
        HOLD_PERIOD_MS = 10000

        def hold_tick(channel):
            try:
                sb.leds.pulse(channel, HOLD_PULSE_MS)
                hold_job[0] = root.after(HOLD_PERIOD_MS, hold_tick, channel)
            except Exception as e:
                hold_job[0] = None
                status.set(f"ERROR (hold stopped): {e}")

        def run(what):
            try:
                mode = FeedbackMode[feedback.get()]
                if what == "off":
                    stop_hold()
                    sb.leds.off(ch.get())
                    status.set(f"LED {ch.get()} off")
                elif what == "hold":
                    stop_hold()
                    ensure_enabled()
                    sb.leds.set_level(ch.get(), level.get(), mode)
                    hold_tick(ch.get())
                    status.set(f"LED {ch.get()} held on at {level.get():g} (re-pulsed every 10 s)")
                elif what == "on":
                    ensure_enabled()
                    sb.leds.set_level(ch.get(), level.get(), mode)
                    sb.leds.on(ch.get())
                    status.set(f"LED {ch.get()} on at {level.get():g}")
                elif what == "pulse":
                    ensure_enabled()
                    sb.leds.set_level(ch.get(), level.get(), mode)
                    sb.leds.pulse(ch.get(), pulse_ms.get())
                    status.set(f"LED {ch.get()} pulsed {pulse_ms.get():g} ms at {level.get():g}")
                elif what == "calibrate":
                    stop_hold()
                    ensure_enabled()
                    status.set(f"Calibrating LED {ch.get()} to {max_current.get():g} A ...")
                    root.update_idletasks()  # blocks for a few seconds and flashes the LED
                    sb.leds.calibrate(ch.get(), max_current.get())
                    status.set(f"LED {ch.get()} calibrated: level 1.0 = 95 % of {max_current.get():g} A")
                elif what == "disable":
                    stop_hold()
                    sb.disable()
                    status.set("System disabled (all outputs safe)")
            except Exception as e:  # e.g. the firmware refusing on() above 30 %
                status.set(f"ERROR: {e}")
            try:
                show_system()
            except Exception as e:
                system.set(f"System: unknown ({e})")

        rows = [("Channel", ttk.Spinbox(root, from_=1, to=8, textvariable=ch, width=6)),
                ("Level (0..1)", ttk.Entry(root, textvariable=level, width=8)),
                ("Feedback", ttk.Combobox(root, textvariable=feedback, width=8, state="readonly",
                                          values=[m.name for m in FeedbackMode])),
                ("Pulse ms", ttk.Entry(root, textvariable=pulse_ms, width=8)),
                ("Max current A", ttk.Entry(root, textvariable=max_current, width=8))]
        for r, (label, widget) in enumerate(rows):
            ttk.Label(root, text=label).grid(row=r, column=0, sticky="w", padx=8, pady=3)
            widget.grid(row=r, column=1, sticky="w", padx=8)
        buttons = ttk.Frame(root)
        buttons.grid(row=len(rows), column=0, columnspan=2, pady=(8, 2))
        for text, what in [("On", "on"), ("Off", "off"), ("Pulse", "pulse"), ("Hold", "hold")]:
            ttk.Button(buttons, text=text, command=lambda w=what: run(w)).pack(side="left", padx=4)
        system_buttons = ttk.Frame(root)
        system_buttons.grid(row=len(rows) + 1, column=0, columnspan=2, pady=(2, 8))
        ttk.Button(system_buttons, text="Calibrate (max A)",
                   command=lambda: run("calibrate")).pack(side="left", padx=4)
        ttk.Button(system_buttons, text="Disable system",
                   command=lambda: run("disable")).pack(side="left", padx=4)
        ttk.Label(root, textvariable=system).grid(row=len(rows) + 2, column=0, columnspan=2, padx=8)
        ttk.Label(root, textvariable=status).grid(row=len(rows) + 3, column=0, columnspan=2, padx=8, pady=(0, 8))
        show_system()

        try:
            root.mainloop()
        finally:
            stop_hold()
            sb.leds.all_off()  # then the context manager disables and closes


if __name__ == "__main__":
    main()
