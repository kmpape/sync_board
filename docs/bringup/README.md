# Board bring-up

Step-by-step procedures for testing a freshly built board at the bench, with a bench power supply, multimeter, and oscilloscope. These are the "does the solder work" guides; day-to-day functional verification is the
`hwtest/checkout.py` script (see [syncboard.md](syncboard.md)).

| Guide | Board |
|-------|-------|
| [syncboard.md](syncboard.md) | SyncBoard (Teensy 4.1 main board) |
| [led-board.md](led-board.md) | LED Control / Drive board |

General principles that apply to every board:

- **Current-limit the supply.** Start low enough to catch a short, high
  enough to let the board actually run; raise it in steps and watch for the
  voltage reaching its target with a stable current draw.
- **Rails before function.** Verify every supply rail and reference at its
  source before trusting anything downstream — a wrong reference makes every
  analog reading wrong while everything "looks" powered.
- **Note the idle current** of a known-good board. Deviation from it is the
  fastest single health check on the next build.
- **Escalate one hardware layer at a time** so a failure is attributable.
- **Record the schematic's own warnings.** The `schematics/*.SchDoc` files
  carry designer notes (jumper rules, "must not connect both", ground
  strategy) that these guides quote but do not replace.
