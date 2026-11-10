# PROTOTYPE: how the Thumbnails come back after a capture (#3)

Throwaway. Not for main. It answers one question: when Tarn hides the Thumbnails
for a capture, how should they come back so the gap reads as intended, not as a blink?

The Thumbnails vanish in one frame (they must: the shot is being taken), stay gone
for the gap, then come back with the new Thumbnail. The new one always springs in
from the left edge, as in Tarn today. The older ones come back in one of four ways:

- **A, Instant:** back in one frame, no motion.
- **B, Spring, together:** every card springs in from the left edge (Tarn's enter spring: `thumbEnterResponse` 0.5 s, `thumbEnterBounce` 0.15).
- **C, Spring, staggered:** the same, newest first, 60 ms apart.
- **D, Quick fade:** fade in with a 20 px nudge from the left, about 0.2 s.

The gaps are the ones the nested bench measured at 2560x1440 (spike/hide-gap):
35 ms (uncompressed shot), 116 ms (fast PNG), 402 ms (PNG, as Tarn does today).
Your own machine may be faster.

## Run it

```sh
./run.sh            # variant B, 116 ms
./run.sh A 402      # variant A, 402 ms
```

Space takes a capture. 1–4 or A–D picks the return, G steps the gap, F goes full
screen (closest to the real thing), Esc quits. The bar at the top also switches
Full screen / Overlay capture, real speed / 4× slower, and "Every 2.5 s" repeats.

## Contact sheet

`./shoot.sh [folder]` renders every variant frame by frame off screen (116 ms gap)
into `sheet.png`. `shots/sheet.png` is the one committed.

## Verdict (2026-11-10)

The user ran it on the real monitor and picked **B with the 35 ms gap**: every older
Thumbnail springs back in from the left edge together, with Tarn's own enter spring,
and the new one springs in with them. It "felt smooth and responsive". The 35 ms gap
is the uncompressed-shot path (grim `-t ppm`, the Thumbnails back at once, the PNG
made off the daemon's main thread), so that is the path the build needs.
