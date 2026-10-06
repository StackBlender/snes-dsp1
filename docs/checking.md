# Checking against the original

`dsp1_check` replays questions and answers recorded from the original chip, byte by byte
through the data register, and reports per command how many answers match:

```sh
build/dsp1_check answers.txt          # DSP1B
build/dsp1_check --dsp1 answers.txt   # the first revision
build/dsp1_check --strict answers.txt # exit nonzero on any difference
```

An answers file has one line per command, hex 16-bit words:

```
<opcode> <inputs...> = <outputs...>
w <words...> =
```

A `w` line writes words with no command (a game ends Raster's stream this way). Lines are
replayed in order, so a Parameter line sets the camera for the Project, Raster and Target
lines after it.

This repository has no answer files: they come from the original chip, which you need to
record yourself (for example by logging the data register in an emulator that runs the
original program).
