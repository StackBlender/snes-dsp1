# How exact it is

Measured against the original chip running its own program in the ares emulator: synthetic
probes (sweeps and random inputs, about 400,000 questions) and every DSP-1 call made by
Super Mario Kart and Pilotwings over three minutes of their attract modes (about 300,000
calls). Both chip revisions were probed: DSP1 (the first cartridges, e.g. Pilotwings) and
DSP1B (the rest); they differ only in Distance.

## Exact

- **Multiply, Triangle, Radius, Range, Rotate, Polar, sine and cosine.** Sine and cosine
  come from a 256-point table with one tangent step between points; the table is generated
  from a formula.
- **Inverse** (1 / (m 2^e) as a mantissa and exponent): a seed per 128 values of the
  mantissa and two Newton steps in the chip's fixed point. The camera commands use it.
- **Attitude, Objective, Subjective, Scalar.**
- **Distance** below r^2 = 2^30, on both revisions: interpolation in a 49-point square-root
  table, generated from a formula. The first revision starts each odd segment one segment
  low.
- **Parameter** up to a zenith angle of about 80°.
- **Raster** and **Target**, every probed input and every game call. Target uses Raster's
  scales for the line at half size, times the screen position as the 16-bit word h × 256
  (so only the low byte of h and v counts).
- **Gyrate** on every Pilotwings call.

## Not yet exact

- **Project:** 99.98% of game calls (Super Mario Kart 50,745 of 50,753; Pilotwings
  73,478 of 73,499). The misses are points very far from the screen's centre (more than
  1,000 units on one axis, where the chip keeps 3 fraction bits), mostly behind the
  camera. With a screen distance (Les) well above the 256 both games use, about 90-99%.
  Two of its rules are measured rather than understood: a point less than half a unit in
  front of the screen gets the screen's depth, and a result between -1/2 and 0 comes out 0.
- **Gyrate** on random inputs: 97.8%, the rest off by one (precision the chip loses inside
  its arithmetic, not yet reproduced).
- **Parameter** past a zenith angle of about 80°, where the chip clamps the view: an
  approximation. No game seen goes there.
- **Distance** from r^2 = 2^30 up (e.g. two components past 23170): the original reads
  past the end of its table into other program data, giving results unlike a square root.
  This one deliberately doesn't copy that; it returns the true root, capped.
- **Busy time.** The class answers at once. How long the original stays busy varies with
  the data (Project 921-1210 master cycles), and that isn't modelled. With the median
  times in the host (see the README), one game's picture differs in 3 of 10,800 frames,
  where its command order shifts by one step at a frame boundary. Giving the host each
  call's exact time doesn't remove them: the game decides by the time left in the frame,
  which also depends on timing within the chip's handling of each byte.
