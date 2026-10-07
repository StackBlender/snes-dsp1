# snes-dsp1

A free implementation of the DSP-1, the maths coprocessor in Super Nintendo cartridges
such as Super Mario Kart and Pilotwings: one C++ class, about 600 lines, under the GPL.
It answers the chip's commands with the same results as the original, so emulators can
run DSP-1 games without the original chip's program, which is the console maker's code.
It's also much faster than emulating the chip running that program (below).

- `src/dsp1/Dsp1.h`, `src/dsp1/Dsp1.cpp`: the chip, seen through its two registers.
- `tools/dsp1_check.cpp`: replays recorded questions and answers through it and reports
  how many it matches ([docs/checking.md](docs/checking.md)).

Made by [StackBlender](https://stackblender.com) for Super Apollo, an emulator that plays
SNES games fast with their music at normal speed.

## How well it matches

Every command, compared answer for answer with the original chip (running in the
[ares](https://github.com/ares-emulator/ares) emulator with the original program, which
isn't in this repository):

| | Exact |
|---|---|
| Multiply, Triangle, Radius, Range, Rotate, Polar, Inverse, sine and cosine | every probed input |
| Attitude, Objective, Subjective, Scalar | every probed input |
| Distance (both chip revisions) | every input below 2^30 (see below) |
| Raster, Target | every probed input and game call |
| Parameter | every probed camera up to a zenith angle of about 80° (past it, approximate) |
| Project | 99.98% of game calls; every call in most probe sets |
| Gyrate | every Pilotwings call; 97% of random inputs |

In games, over three minutes of each attract mode, the picture is identical to the
original's frame for frame in Super Mario Kart, and in Pilotwings for all but 3 of 10,800
frames (from the chip's busy time, below). [docs/exactness.md](docs/exactness.md) has the
details and what isn't exact yet.

## Speed

The class works out each command's answer directly, without emulating the chip's
processor running its program. So a DSP-1 game costs about what a game without a
coprocessor costs. Emulating the chip instruction by instruction, kept in step with the
main processor, takes about 40% of the time of a Super Mario Kart race. In the same
emulator on the same desktop, with frames not drawn, a race runs at 9.7 times real time
with this class and 6.0 times when emulating the chip; Pilotwings in flight runs at 10.2
and 5.6 times.

The trade-off is timing. Emulating the program reproduces exactly how long the chip stays
busy after each command, and this class doesn't (see "Using it" and
[docs/exactness.md](docs/exactness.md)).

## How it was made

Clean room: written from public descriptions of the commands (what each takes and
returns), then refined by black-box measurement, sending the original chip millions of
questions and comparing its answers. Nobody on this project has read the chip's program
or a disassembly of it. Where the chip's own tables matter (sine, square root), the code
generates them from formulas that reproduce every value the chip gives; it holds no data
taken from the chip.

## Using it

```cpp
dsp1::Dsp1 chip(dsp1::Dsp1::Revision::Dsp1B); // Revision::Dsp1 for the first cartridges (e.g. Pilotwings)
chip.writeData(byte);         // the data register, a byte at a time
uint8_t b = chip.readData();
uint8_t s = chip.readStatus(); // bit 7 ready, bit 4 half a word done, bit 2 awaiting a command
```

Map the data and status registers where the cartridge board puts them. The class answers
at once; `takeWork()` reports what the chip just started, for a host that models how long
the original stays busy (some games wait on the ready bit). Typical busy times, in SNES
master cycles after the last parameter byte: Parameter about 1470, Project about 1100,
Target 352, Raster about 360 per line, Attitude about 290, Gyrate 260-460, Distance
180-240, Inverse about 115. The original's times vary with the data, which this doesn't
model.

```sh
cmake -S . -B build && cmake --build build
```

## License

GNU General Public License, version 3 or (at your option) any later version; see
[LICENSE](LICENSE). For other terms, contact [StackBlender](https://stackblender.com).
