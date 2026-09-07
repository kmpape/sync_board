# Board bring-up

How to test a freshly built board at the bench with a power supply,
multimeter, and scope. These guides check that the solder is good; for
day-to-day functional testing use `hwtest/checkout.py` instead.

| Guide | Board |
|-------|-------|
| [syncboard.md](syncboard.md) | SyncBoard (Teensy 4.1 main board) |
| [led-board.md](led-board.md) | LED Control / Drive board |

Some things that apply to every board:

- Current-limit the supply. Set it low enough to catch a short but high
  enough to let the board run, then raise it in steps and watch the voltage
  reach its target with a steady current draw.
- Check the rails and references at their source before trusting anything
  downstream. A wrong reference reads fine on the "3V3" net but makes every
  analog measurement wrong.
- Write down the idle current of a good board. If the next build draws
  something different, that's the first clue.
- Add one thing at a time so you can tell what failed.
- The schematics (`schematics/*.SchDoc`) have the designer's own notes on
  jumpers and ground. These guides quote the important ones but read the
  schematic too.
