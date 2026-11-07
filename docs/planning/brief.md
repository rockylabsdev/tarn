# Tarn: brief for the wayfinder map

## The idea

Build my own CleanShot X for Linux. I use CleanShot on my Mac and want the same capture-annotate-share flow on my Hyprland desktop. The reference screenshots, numbered below, are not kept in this repository.

## What the references show

**Annotation editor** (03–15). One window after a capture, with:

- Tools: select, rectangle (outline and filled), ellipse, line, arrow, text (with emoji), pixelate, highlight, counter (numbered steps), pencil, and one more tool next to the pencil whose purpose isn't clear from the captures.
- A color picker and stroke styles, with per-tool options (arrow style, font size, pixelate and highlight strength).
- Crop with aspect presets, exact size, rotate and flip, snap to edges.
- A background panel that frames the shot for sharing: gradients, wallpapers, blurred versions of the shot, plain colors, padding, inset, shadow, rounded corners, alignment, aspect ratio, saved presets.
- Bottom bar: zoom, a "Drag Me" handle to drag the image into another app, share, pin to screen, copy, cloud upload. Save as / Done.

**Screen recording** (16–18). Area recording to MP4 or GIF, and a converter with trim, quality, dimensions and audio options.

**Studio Mode** (19–25). Recording as an editable project: a timeline where you click or drag to add zooms, cursor styling and smoothing, click effects, keystroke display, microphone and system audio, camera overlay, the same backgrounds as the editor, motion blur, and smooth or dynamic zoom animation.

## Not in the references

These are CleanShot features I may want but didn't capture: the area-selection overlay itself (crosshair, magnifier, dimensions), the floating thumbnail after a capture, capture history, scrolling capture, text recognition (OCR), self-timer, and hiding desktop icons.

## What exists on Linux already

grim and slurp (capture and region select), grimblast (Hyprland wrapper), Satty and swappy (annotation), wf-recorder and wl-screenrec (recording), hyprpicker (color picker), wl-clipboard. Tarn might build on these or replace them; that's a decision for the map.

## Possible ties to Island

My shell project, Island (`rockylabsdev/island`), plans "Island activities": a transient circle for things like a recording in progress. A Tarn recording could show up there.

## Open from the start

- What the destination is: a spec for a first version, and which of the features above it covers.
- Which features matter most to me, and which are out of scope (cloud upload is a likely candidate).
- Language and toolkit, and how much to reuse from existing tools.
