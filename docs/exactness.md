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
- **Memory size** (2F): the chip's version, 1.00 on the first revision and 1.01 on DSP1B. The
  memory test (0F) answers 0. The memory dump returns the chip's internal data, which isn't
  reproduced.
- **Distance** below r^2 = 2^30, on both revisions: interpolation in a 49-point square-root
  table, generated from a formula. The first revision starts each odd segment one segment
  low.
- **Parameter** up to a zenith angle of about 80°.
- **Raster** and **Target**, every probed input and every game call below the tilt limit.
  Raster stops when a written word completes a line (games write four words after whole
  lines, or one after three).
- **The data register** with no results due reads $80. Target uses Raster's
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
- **Parameter** past a zenith angle of about 80°, where the chip clamps the view. The
  limit depends on the eye's height: one of 16 values (14516 to 14563) by how far the
  height must be shifted to normalise it. The eye follows the true angle, while the
  ground point and the horizon use the limit; Vof is the true angle's horizon minus Vva.
  Exact with large screen distances; with a screen distance of 256, Vva and Vof can be one
  off and Cy a few units off. Project uses the true angle throughout and is exact there.
- **Raster** past the limit: the chip uses an effective cosine, its own stored cosine for
  the limit (one per height bucket, measured) over the cosine of how far past the limit the
  camera is, for the screen's distance and the scale along the view; each line's offset
  uses the true angle's sine. The scale across is exact; the scale along is within one
  unit.
- **Distance** from r^2 = 2^30 up (e.g. two components past 23170): the original reads
  past the end of its table into other program data, giving results unlike a square root.
  This one deliberately doesn't copy that; it returns the true root, capped.
- **Busy time.** The class answers at once. How long the original stays busy varies with
  the data (Project 921-1210 master cycles), and that isn't modelled. With the median
  times in the host (see the README), one game's picture differs in 3 of 10,800 frames,
  where its command order shifts by one step at a frame boundary. Giving the host each
  call's exact time doesn't remove them: the game decides by the time left in the frame,
  which also depends on timing within the chip's handling of each byte.
