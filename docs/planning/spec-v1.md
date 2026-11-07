# Tarn v1: the screenshot and recording flow

Terms are as defined in [GLOSSARY.md](../../GLOSSARY.md); the why behind the stack is in [ADR 0001](../adr/0001-qt6-cpp-qml-own-program.md) and [ADR 0002](../adr/0002-island-connects-to-tarn.md). The decisions below were made one at a time before this spec gathered them.

## Problem Statement

The user works on a Mac and on a desktop running Hyprland (Wayland) on CachyOS, with one 2560×1440 monitor. On the Mac, a screenshot is one key away from a framed, annotated image in the clipboard, and a short screen recording is one key away from an MP4 or GIF. On Linux they have a stopgap: Island's Print keys run `grim`, `slurp` and `swash`. That gives a frozen-less area pick, a basic annotator, and nothing else: no window capture, no floating preview to act on, no framing for sharing, no pin, no History, no text capture, no timer, and no recording at all. No existing Linux tool covers the whole flow, and the nearest ones either don't match Island's look or are GPL code Tarn can't borrow from.

## Solution

**Tarn** is its own Qt6 program (C++ core, QML screens, MIT license) that brings a one-key capture-annotate-share flow to Hyprland, in Island's colors and style. An always-running `tarn daemon` makes every surface open instantly. Global keys in a Hyprland file Tarn ships run short `tarn` commands.

- **Capture:** Print opens the **Overlay** on a frozen image of the screen. Point at a window to take it, or drag an area. Super+Print takes the full screen, and Shift+Print repeats the last area.
- **After capture:** nothing happens automatically. Every capture lands as a **Thumbnail** in the bottom-left corner, with Copy, Save, Pin and Edit.
- **Edit:** the **Editor** has 12 tools, Crop, Add image, undo, and a Background panel that puts a **Frame** round the shot. A **Preset** can be applied to every capture.
- **Pin:** floats a capture above every window.
- **Extras:**
  - **History** keeps every image capture's untouched original for a month.
  - **Text capture** copies the text in an area.
  - The **Timer** takes the shot after a 3 s **Countdown**.
- **Recording:** Ctrl+Print records an area to MP4 (optionally with mic and system audio) or GIF, with optional click **Ripples**. A **Recording bar** gives stop, pause, restart and delete, and Island shows the Recording in its Circle when it's running. A fresh Recording can be **Trimmed**.
- **Settings:** one hand-edited settings file. What Tarn remembers from last use is kept separately.

## User Stories

### Capturing

1. As the user, I want Print to open the Overlay on a frozen image of my screen, so that what I'm capturing doesn't change while I pick it.
2. As the user, I want the Overlay to appear within about 50 ms of pressing Print, so that capturing feels instant.
3. As the user, I want the Overlay to cover everything on screen, Island included, so that I can capture any part of the screen.
4. As the user, I want the frozen image to be pixel-identical to my screen, so that the capture is exactly what I saw.
5. As the user, I want my mouse pointer kept out of screenshots, so that captures look clean.
6. As the user, I want pointing at a window to highlight it with an outline that follows its rounded corners and a pill naming the app, so that I know a click takes that window.
7. As the user, I want the window highlight to stay put while my pointer crosses the gap between two windows, so that the screen doesn't flash.
8. As the user, I want clicking a highlighted window to capture just that window, without its border or gap and with transparent rounded corners, so that it looks like the window itself.
9. As the user, I want anything covering a window when I capture it (an open menu) to appear in the capture, so that it matches what I saw.
10. As the user, I want to drag out an area and have it captured the moment I let go, so that most captures are a single gesture.
11. As the user, I want holding Ctrl while letting go to keep the Selection on screen, so that I can adjust it before taking it.
12. As the user, I want to move a kept Selection by dragging inside it, resize it with its Handles, move it with the arrow keys and resize it with Alt+arrows, so that I can place it exactly.
13. As the user, I want Enter, a double-click or the Capture button to take a kept Selection, and Esc to clear it, so that I can confirm or retry.
14. As the user, I want Shift to draw a square, Alt to draw from the center, and held Space to move the Selection while dragging, so that drawing is fast and exact.
15. As the user, I want the arrow keys to move the real pointer 1 px (10 px with Shift), and Enter, arrows, Enter to select an area by keyboard, so that I can capture without the mouse.
16. As the user, I want Esc or a right-click to cancel the Overlay, so that I can back out at any time.
17. As the user, I want a size pill above the Selection showing W × H (and x, y while adjusting), so that I can see exactly what I'm taking.
18. As the user, I want the area outside the Selection dimmed by 50% with an accent outline round it, so that the Selection stands out.
19. As the user, I want M to turn on a Magnifier beside my pointer, so that I can put an edge on an exact pixel.
20. As the user, I want the Magnifier to stay on or off as I left it for the next capture, so that I don't toggle it every time.
21. As the user, I want the Magnifier to show 15 pixels at 8x with a grid, the pixel under the pointer boxed, and my x, y in a pill underneath, so that I can read my position precisely.
22. As the user, I want the Magnifier to flip to the other side near a screen edge, so that it never runs off screen.
23. As the user, I want Super+Print to capture the whole screen immediately, so that a full-screen shot needs no Overlay.
24. As the user, I want Shift+Print to capture the last area again, even after a restart, so that I can repeat a shot quickly.
25. As the user, I want Shift+Print with no saved area to open the Overlay, so that the key always does something useful.
26. As the user, I want B in the Overlay to skip my auto-applied Preset for this capture, with a "No frame" pill to confirm, so that I can take an unframed shot.

### After a capture

27. As the user, I want every capture to appear as a Thumbnail in the bottom-left corner, without anything copied or saved automatically, so that I decide what happens to it.
28. As the user, I want the Thumbnail to appear instantly and never take keyboard focus, so that I can keep typing in the app I'm in.
29. As the user, I want every Thumbnail to be 240 × 150 with the capture cropped to fill it (a tall capture keeps its top), so that the stack looks tidy.
30. As the user, I want hovering a Thumbnail to show Copy and Save in the middle and ×, Pin and Edit in the corners, so that every action is one click.
31. As the user, I want clicking a Thumbnail to open the Editor, so that editing is one click away.
32. As the user, I want Copy and Save to show a check before the Thumbnail slides away, so that I know it worked.
33. As the user, I want Copy to put both the image and a PNG file on the clipboard, so that chat apps paste the image and file managers paste a file.
34. As the user, I want copied files to stay readable after the Thumbnail closes, so that a late paste still works.
35. As the user, I want Save to write a PNG to my screenshot folder named by capture time, so that my files sort by when I took them.
36. As the user, I want window captures named with the app (`2026-11-02_14-03-22_firefox.png`), so that I can find them later.
37. As the user, I want a name clash to add `_2`, `_3` and so on, never overwriting a file, so that nothing is lost.
38. As the user, I want a right-click on a Thumbnail to offer Save as…, Delete and Close all, so that less common actions are close by.
39. As the user, I want Delete to send the History original to the Trash as well, so that a capture I regret is gone.
40. As the user, I want closing a Thumbnail (×, Copy, Save, Save as…, Pin, Delete) to slide it off the left edge, so that exits feel calm and consistent.
41. As the user, I want Thumbnails to stack upward, newest in the corner, at most 4, so that the stack never takes over the screen.
42. As the user, I want a fifth capture to auto-save the oldest Thumbnail (edits and Frame included) and slide it out, so that an overflow never loses a capture.
43. As the user, I want a Thumbnail whose auto-save failed to stay, with a notification saying why, so that I can fix it.
44. As the user, I want Thumbnails never to close on their own unless I set a delay, so that a capture waits for me.
45. As the user, I want Super+Ctrl+Print to bring back the last closed Thumbnail, edits included, so that I can undo a close.
46. As the user, I want moving the pointer between Thumbnails never to flicker their controls, so that the stack feels solid.
47. As the user, I want a Thumbnail to be hidden from the stack while its Editor is open, and not to count toward the 4, so that the stack reflects what's waiting.

### Editor

48. As the user, I want the Editor to open instantly, floating and centered, sized to the shot between about 1280 × 806 and 1690 × 1008, so that it fits my screen without resizing.
49. As the user, I want the Editor laid out as a top toolbar, a bottom bar and a Background panel on the left, in Island's colors, so that it feels familiar and matches my desktop.
50. As the user, I want all 12 tools (Select, Rectangle, Filled rectangle, Ellipse, Line, Arrow, Text, Pixelate, Spotlight, Counter, Pencil, Highlighter), so that I have every tool I need to mark up a shot.
51. As the user, I want single-letter keys for each tool (V R F O L A T X S N P H) and C Crop, B Background, I Add image, so that I can switch without the mouse.
52. As the user, I want every Editor to open on Rectangle, so that the most common tool is ready.
53. As the user, I want one color shared by every tool, and picking a color to recolor the selected Annotation, so that colors stay consistent.
54. As the user, I want a color popover with 8 fixed swatches, Island's Palette swatches, a custom picker with opacity, and an eyedropper that samples the shot, so that I can match any color.
55. As the user, I want Rectangle, Ellipse, Line, Arrow and Pencil to share one line width, and Text, Counter and Highlighter to keep their own sizes, so that a box and its arrow match.
56. As the user, I want `[` and `]` to step the current tool's size, so that resizing is quick.
57. As the user, I want my color, sizes, styles and strengths remembered across captures and restarts, so that I don't set them up each time.
58. As the user, I want only the current tool's options in the top bar, so that the toolbar stays uncluttered.
59. As the user, I want a tool to stay active until I pick another, and clicking an existing Annotation to select it, so that drawing several shapes is fast.
60. As the user, I want selected Annotations to show the Overlay's white dots with an accent rim (eight on boxes, one at each end of a line, an outline only on Text, Counter, Pencil and Highlighter), so that Tarn has one handle look.
61. As the user, I want hovering an unselected Annotation to show a faint ghost of its selection, so that I know a click will pick it.
62. As the user, I want to move, resize, recolor, restyle (any option changed with an Annotation of that tool selected applies to it), delete (Delete or Backspace), duplicate (Ctrl+D or Alt-drag), nudge (arrows, Shift for 10 px), select all (Ctrl+A) and reorder (Ctrl+] / Ctrl+[) Annotations, so that I can fix anything after drawing it.
63. As the user, I want to drag a box to select several Annotations, and to copy and paste Annotations between Editors, so that I can work on several at once.
64. As the user, I want Shift to constrain shapes and lines while drawing and to lock moves to one axis, and Alt to draw from the center, so that drawing is precise.
65. As the user, I want three arrow styles (tapered, open head, double-headed), so that arrows suit the shot.
66. As the user, I want Text in Island's UI font with plain, outlined and label styles, multi-line with Enter, and a searchable emoji picker, so that labels read clearly.
67. As the user, I want Pixelate to hide the picture beneath it (pixelate, blur or black out, with strength) but never my Annotations, and to be irreversible in the exported image, so that hidden details stay hidden.
68. As the user, I want Spotlight to dim everything outside a rounded rectangle or ellipse, with several Spotlights sharing one dim layer, so that I can draw the eye.
69. As the user, I want each Counter click to place the next number (1 2 3, A B C or i ii iii, from any start), renumbering when one is deleted, so that steps stay in order.
70. As the user, I want Pencil strokes smoothed, so that freehand looks tidy.
71. As the user, I want the Highlighter to be a see-through, flat-tipped marker that darkens what's under it, straight with Shift, so that it looks like a real highlighter.
72. As the user, I want Crop with freeform or aspect presets, W × H fields, rotate, flip, an output scale and snap to edges, so that I can shape the final image.
73. As the user, I want Crop to destroy nothing while I edit, so that I can widen it again later.
74. As the user, I want to add an image from the toolbar, by dropping a file or with Ctrl+V, as an Annotation that can grow the canvas, so that I can combine shots.
75. As the user, I want unlimited undo and redo (Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y) covering Annotations, Crop and Frame changes, so that I can experiment freely.
76. As the user, I want the zoom menu (Fit, 25–400%), Ctrl+scroll zoom, Ctrl+0 / Ctrl+1 / Ctrl+= / Ctrl+−, and Space-drag or middle-drag to pan, so that I can work on detail.
77. As the user, I want the Editor to open at Fit, never above 100%, so that I see the whole shot first.
78. As the user, I want Done (or Enter when not typing, or Ctrl+Enter) to return to the Thumbnail with my edits, still editable, so that I can come back to it.
79. As the user, I want Esc to step back one level (finish Text, leave Crop, deselect, then cancel the Editor without a prompt), so that Esc never throws away more than I mean.
80. As the user, I want Copy in the bottom bar (or Ctrl+C with nothing selected) to copy and close both the Editor and the Thumbnail, so that the common path is one action.
81. As the user, I want Ctrl+Shift+C to copy and keep the Editor open, so that I can copy a draft and keep working.
82. As the user, I want Ctrl+S to save to my screenshot folder and close both, so that saving is one key.
83. As the user, I want Save as… (Ctrl+Shift+S) to open the system file dialog with PNG, JPEG or WebP, starting in my last folder, so that I can save anywhere.
84. As the user, I want Pin (Ctrl+P) from the Editor to pin the edited image, so that I can float my annotated shot.
85. As the user, I want every button's tooltip to show its key, so that I learn the keys as I go.
86. As the user, I want to move the Editor by dragging an empty part of a bar, so that it needs no title bar.

### Frame and Presets

87. As the user, I want a Background panel offering None, gradients, wallpapers, blurred copies of the shot and plain colors, so that I can frame shots for sharing.
88. As the user, I want Tarn's own image gradients plus gradients from Island's Palette when Island is running, so that frames can match my wallpaper.
89. As the user, I want my current wallpaper first, then my wallpaper folder's images, then "+" for any image, so that I can use my own pictures.
90. As the user, I want three blurred tiles (light, heavy, heavy darkened) made from the shot itself, so that a frame can echo its content.
91. As the user, I want fixed swatches, a transparent swatch, a custom color and Palette swatches for plain backgrounds, so that I can choose any flat fill.
92. As the user, I want Padding, Inset, Auto-balance, Shadow, Corners, Alignment and Ratio controls, sized relative to the shot, so that a Preset looks the same on any capture.
93. As the user, I want to save Frames as named Presets in a readable file, so that I can reuse them and edit them by hand.
94. As the user, I want to mark one Preset to apply to every capture, so that my shots come out framed automatically.
95. As the user, I want a Preset using a Palette gradient or the current wallpaper to follow my wallpaper's colors, so that frames stay in tune with my desktop.
96. As the user, I want a Preset whose wallpaper file was deleted to fall back quietly, so that it never breaks a capture.

### Pin

97. As the user, I want a Pin to appear where it was captured, at its original size, above every window and on every workspace, so that I can keep a reference in view.
98. As the user, I want to move a Pin by dragging, resize it by scrolling and change its opacity with Ctrl+scroll, so that it fits around my work.
99. As the user, I want middle-click, its hover × or Esc to close a Pin, so that it's easy to dismiss.
100. As the user, I want a Pin's right-click menu to offer Copy, Save as…, Edit and Close all pins, and Ctrl+C to copy it, so that a Pin is still a capture I can use.

### Extras

101. As the user, I want every image capture's untouched original kept in History for a month, so that I can recover a shot I didn't save.
102. As the user, I want History to be a plain folder of PNGs with a small description file each, so that I can browse it in a file manager until a History window exists.
103. As the user, I want Ctrl+Shift+Print to let me select an area and copy its text, so that I can grab text from images and apps that won't let me select it.
104. As the user, I want lines within a paragraph joined and paragraphs kept apart in copied text, so that it pastes cleanly.
105. As the user, I want a notification with the first line of copied text, or "No text found", so that I know whether it worked.
106. As the user, I want Super+Shift+Print to start a Timer (drag an area or press F for the full screen), so that I can capture menus and hover states.
107. As the user, I want the Countdown shown in a corner of the chosen area, never taking focus and gone before the shot, so that an open menu stays open and the Countdown never shows in the capture.
108. As the user, I want pressing the Timer key again during the Countdown to cancel it, so that I can back out.

### Recording

109. As the user, I want Ctrl+Print to open the Overlay to select an area to record, so that recording starts like a screenshot.
110. As the user, I want the Record panel to appear instantly over the kept Selection, with W × H fields, a settings button, and mic, system audio and Show clicks toggles, so that I can set up the Recording in place.
111. As the user, I want Enter to start an MP4 and Alt+Enter a GIF, so that I choose the format per Recording.
112. As the user, I want mic and system audio for MP4s (grayed for GIFs, with a tooltip saying GIFs have no sound), so that I can narrate a demo.
113. As the user, I want Show clicks to draw a Ripple where I press a mouse button inside the area, so that viewers can follow my clicks.
114. As the user, I want my mic, system audio and Show clicks choices remembered, so that I set them once.
115. As the user, I want the area's outline to turn red at once and the Recording bar to appear only once the recorder is actually writing (about 0.45 s), so that I start acting when the Recording really starts.
116. As the user, I want the Recording bar outside the area (below, else above, right or left) with stop and elapsed time, pause, restart, delete and a grip, so that the controls never appear in the video.
117. As the user, I want to drag the Recording bar anywhere for this Recording, so that it doesn't cover what I need.
118. As the user, I want the Recording bar never to take focus, and to ignore clicks for its first 0.5 s, so that it never disturbs the app I'm recording.
119. As the user, I want Ctrl+Print to stop a Recording, and Esc never to stop one, so that the recorded app can still use Esc.
120. As the user, I want pause and resume, so that I can skip a boring stretch.
121. As the user, I want restart to throw the Recording away and start again at 0:00, so that I can retry a take quickly.
122. As the user, I want delete to throw the Recording away with no Thumbnail and no confirmation, so that a bad take simply vanishes.
123. As the user, I want stopping a GIF to show "GIF 42%" progress on the Recording bar while it's made, so that I know it's working.
124. As the user, I want MP4s at 60 fps and GIFs at about 20 fps, full size, made with gifski, so that both look their best.
125. As the user, I want the pointer shown in Recordings by default, with a Setting to hide it, so that viewers can follow me.
126. As the user, I want a finished Recording to land as a Thumbnail with a format-and-length chip, Copy, Save and Trim (no Pin), so that it follows the same flow as a screenshot.
127. As the user, I want Copy on a Recording to put the file on the clipboard, and Save to write it to my Recordings folder, so that I can share or keep it.
128. As the user, I want a Recording's Thumbnail never to close on its own, so that a Recording is never lost to a timeout.
129. As the user, I want unsaved Recordings left over from a crash to be saved to my Recordings folder at the next start, with one notification, so that a crash never loses a take.
130. As an Island user, I want a running Recording to show as a red dot in Island's Circle, with one-click stop, so that I can see and stop it at a glance.
131. As an Island user, I want Island to show paused and GIF-encoding states, and to pick the Recording up after Island restarts, so that it always reflects the truth.

### Trim

132. As the user, I want clicking Trim on a Recording's Thumbnail to open the Trim window instantly, floating and centered, so that I can cut its start and end.
133. As the user, I want a filmstrip in an accent frame whose thick ends are the Handles, with time ticks beneath, so that I can see both content and time while cutting.
134. As the user, I want the preview to show the frame under a Handle while I drag it, with a time pill, snapping to whole frames, so that I can cut exactly.
135. As the user, I want to scrub by pressing and dragging on the filmstrip, so that I can find a moment quickly.
136. As the user, I want Space to play the kept range on a loop, I / O to set the start / end at the Playhead, ←/→ to step a frame (Shift for a second) and Home / End to jump, so that I can trim by keyboard.
137. As the user, I want Done (Enter) to trim with a progress bar and Cancel, so that a slow GIF re-make can be stopped.
138. As the user, I want Copy (Ctrl+C) and Save as… (Ctrl+S) in the Trim window, and Esc to close it and drop the trim, so that I can finish from there.
139. As the user, I want a GIF's Trim to re-make the GIF from the kept video, so that I never convert anything by hand.
140. As the user, I want Trim to change only Tarn's copy, never a file I already saved or pasted, so that trimming is safe.

### Settings, keys and setup

141. As the user, I want one commented settings file written on first start, so that I can see every Setting and its default.
142. As the user, I want edits to that file applied as soon as I save it, so that I never restart Tarn.
143. As the user, I want a mistake in the file to show one notification naming the line while the rest still applies, so that a typo never breaks Tarn.
144. As the user, I want `tarn settings` (and the Record panel's settings button) to open the file in my text editor, so that I can find it.
145. As the user, I want Tarn never to rewrite my settings file, so that my comments and edits survive.
146. As the user, I want shortening History's retention to send the newly expired entries to the Trash, so that a typo is recoverable.
147. As the user, I want Tarn's global keys in one Hyprland file I can edit to rebind, so that keys work like the rest of my desktop.
148. As the user, I want Tarn to replace Island's Print stopgap cleanly, so that the two never fight over a key.
149. As the user, I want Tarn's colors to follow Island's Palette live, with a fallback when Island isn't running, so that Tarn always matches my desktop and still works alone.
150. As the user, I want every keyboard-opened surface to appear with no open animation, and controls to change state instantly, so that Tarn feels snappy.
151. As the user, I want the Overlay, Editor, Thumbnail, Record panel, Recording bar, Magnifier and Trim window built from the same parts, so that Tarn feels like one app.
152. As the future open-source user, I want nothing in Tarn hard-wired to one screen size or to the original author's files, so that it works on my setup too.

## Implementation Decisions

### Shape of the program (ADR 0001)

- One program, `tarn`, built with CMake: Qt6, a C++ core and QML for every screen, with LayerShellQt for full-screen and floating layer surfaces. MIT license. Omasnap (MIT) is the guide for the Hyprland parts and Satty (MPL-2.0) for the Editor, and small pieces of either may be copied. No GPL code is copied or linked.
- **`tarn daemon`** runs for the whole session and owns every surface and all state. The other `tarn` subcommands are thin clients: each sends one request to the daemon over a local socket and exits. Subcommands:
  - capture area, full screen, repeat;
  - record (start or stop);
  - text;
  - timer (start or cancel);
  - restore;
  - settings;
  - the internal click notice used by Show clicks.
- **Starting at login:** the daemon is started from Tarn's Hyprland file. This spec makes that choice; the map left "how it starts at login" open.
- **Warm-up:** at start-up the daemon maps the Overlay once, invisibly. Without this the first open after login takes 120–160 ms instead of 40–55 ms.
- **Outside programs, all run as separate commands, never linked:**

| Program | Used for |
|---|---|
| `grim` | screenshots |
| `tarn-clip` (Tarn's own helper, ext-data-control-v1) | the clipboard: `wl-copy` offers one type per call, so Tarn's helper offers the image and the file together |
| `gpu-screen-recorder` | recording (NVENC, region given as `-w region -region WxH+X+Y`, the form 6.1.5 documents) |
| `gifski` | GIFs (quality 100, required: no ffmpeg fallback; the MP4 behind a GIF is recorded with `-q ultra`) |
| `ffmpeg` | Trim (CPU x264 veryfast, crf 20, frame-exact) |
| `tesseract` | Text capture |
| `hyprctl` / Hyprland's sockets | windows, pointer position, live Lua, pointer warping |

  Install `gpu-screen-recorder` from Arch's `extra` repository, because the CachyOS mirror's signature was missing on 2026-11-02.

### Modules (deep modules, small interfaces)

- **Hyprland bridge:** everything Tarn asks of Hyprland:
  - window list and geometry for the Overlay's highlight;
  - pointer position;
  - pointer warping for keyboard selection;
  - live Lua through `hyprctl eval`, to add and remove Show clicks' mouse binds;
  - whether the pointer is drawn in hardware.
- **Screen grabber:** wraps grim for the frozen image, full screen, window and Timer shots. It waits until a closing Overlay is off screen before grabbing again.
- **Overlay model:** holds the state of the Selection:
  - modes: area, record, timer and text;
  - hover and the window highlight with its 16 px stickiness;
  - drag modifiers;
  - letting go to capture, or keeping the Selection with Ctrl;
  - keyboard selection;
  - the Magnifier on or off;
  - the B skip.
  It turns pointer and key events into a confirmed Selection or a cancel.
- **Capture store:** creates a capture from a grabbed image and hands it to History and the Thumbnail stack. It applies the auto-applied Preset unless it was skipped.
- **Thumbnail stack:** at most 4 entries and overflow auto-save. It also handles hidden-while-editing, the auto-close Setting (never for Recordings), close reasons and the single `tarn restore` slot, which holds edits and the kept Recording.
- **Capture document:** the shot, its Annotations, the Crop (rotate, flip, output scale), the Frame and the undo history.
  - Owns the rules that matter for correctness:
    - Pixelate hides only the picture, and drawn Annotations stay on top;
    - pixelation is randomized so it can't be reversed: flat blocks, each taking a random pixel's color plus a small shift, with no per-pixel noise;
    - several Spotlights share one dim layer, which covers the shot but not the Frame;
    - Counters renumber when one is deleted;
    - Crop destroys nothing, and Annotations outside it are kept but hidden;
    - an added image that reaches past the shot grows the canvas.
  - Flattens to an image for Copy, Save, Pin and overflow. A document lives in memory only; there's no project file.
- **Frame and Preset store:**
  - background kinds: Tarn's 20 bundled image gradients by name, Palette gradients by slot, current wallpaper, wallpaper folder, "+" images by path, blurred tiles, plain colors and transparent;
  - controls: Padding, Inset, Auto-balance, Shadow, Corners, Alignment, Ratio;
  - Padding (up to 1× the shot's shorter side), Inset, Shadow and Corners are fractions of the shot's size, and Alignment always moves the shot, with or without a Ratio;
  - Annotations can be drawn on the Frame's background as well as the shot; Pixelate and the Spotlight dim stay on the shot;
  - reads and writes the hand-readable Presets file;
  - fallbacks: a missing Palette falls back to the closest image gradient by average color, and a deleted wallpaper falls back with a quiet mark.
  - Auto-balance is the one control allowed to slip if it proves hard.
- **Storage and naming:**
  - finds the XDG Pictures and Videos folders, and creates a missing folder on first save;
  - names files by capture time, plus the app name for window captures, plus `_2` and so on for a clash;
  - writes PNG, or JPEG and WebP through Save as…;
  - Copy writes the finished file to a shared cache folder, and the sweep deletes those files after 1 day.
- **History store:**
  - writes in the background right after each image capture, Timer shots included, never Text capture;
  - each entry is the untouched PNG plus a description file: capture time, kind, app and window title for window captures, screen position, and every path it was saved to;
  - lives in XDG data, not cache;
  - the daemon sweeps at start-up and every 6 hours, by capture time, deleting permanently;
  - a shortened retention sends the newly expired entries to the Trash on the first sweep after it;
  - Thumbnail Delete sends the entry to the Trash, and `tarn restore` puts it back.
- **Clipboard:** offers `image/png` and a PNG file together for screenshots, and a file for Recordings, through `tarn-clip`, Tarn's own detached clipboard helper (`wl-copy` can offer only one type per call), so the clipboard outlives Tarn.
- **Recording session:**
  - states: idle → starting → recording ⇄ paused → encoding (GIF only) → done or deleted;
  - starts `gpu-screen-recorder` when the Record panel confirms;
  - it's "recording" only once the recorder's output file exists;
  - pause stays disabled until then, because a pause signal sent earlier kills the recorder;
  - restart, delete, stop;
  - mic and system audio for MP4 only, and the pointer per the Setting;
  - GIF: the MP4 is kept, and gifski makes the GIF beside it.
  - Kept files live in a cache folder on disk (not `/tmp`, which is RAM here). They're deleted once the Recording can no longer be restored, and recovered to the Recordings folder at start-up after a crash.
- **Click Ripples:** while a Recording with Show clicks on runs:
  - the Hyprland bridge adds non-consuming left, middle and right mouse binds that send the click notice;
  - the daemon reads the pointer position and draws a Ripple if the press is inside the area;
  - the binds are removed when the Recording ends;
  - the Ripples are drawn on a click-through layer surface that isn't on screen at any other time.
- **Trim:**
  - keeps a range snapped to whole frames (60 fps MP4, 20 fps GIF);
  - re-encodes the kept Recording in place with progress and Cancel, then for a GIF re-makes the GIF from the kept MP4;
  - never touches a file that was already saved or copied.
- **Text capture:** runs Tesseract on the Selection with the configured languages. It joins lines within a paragraph, keeps blank lines between paragraphs, copies the result, and notifies the first line or "No text found".
- **Timer:** after the Selection (or F for the full screen), shows the Countdown in a corner of the area, focus-free. Then it hides the Countdown, grabs the live screen and hands the shot to the Capture store. Pressing the Timer key again cancels.
- **Island bridge:** listens on the per-session socket and broadcasts Recording state lines. On connect it sends the current state, and it accepts `stop`. The contract is [docs/shell-socket.md](../shell-socket.md). Island's side is built; when no Island is connected, Tarn behaves as if Island isn't running.
- **Settings:** reads the settings file and writes it once, with comments, if it's missing. It watches the file and applies changes live. A bad value notifies once with the line, and only that Setting keeps its last good value.
- **Remembered state:** the machine-written state file holds:
  - the last area;
  - tool color, sizes, styles and strengths, and the Counter style;
  - mic, system audio and Show clicks;
  - the Magnifier;
  - the last Save-as folder.
  It's never mixed with Settings.
- **Palette:** reads Island's `palette.json` live, with a built-in fallback. Tarn keeps its own copy of Island's style values (radii, spacing, motion) and the Geist font.
- **Notifications:** desktop notifications, used only for failures, Text capture results and crash recovery. There's no notification or sound for Copy or Save.

### Screens (QML, built from the Editor's parts: 32 px capsules, IconBtn, Sep, Tabler icons at 16 px)

- **Overlay** (look variant D, as restyled by the Magnifier's look):
  - a layer surface on the overlay layer, exclusive zone −1, exclusive keyboard;
  - 50% dim, an accent outline, and the window highlight following Hyprland's rounding;
  - dark surface pills for size (and x, y while adjusting), app name and Cancel / Capture;
  - white 14 px Handle dots with a 2 px accent rim, and a 3 px accent outline at 50% round the Selection;
  - the system crosshair pointer, with no crosshair lines.
- **Magnifier:**
  - a 120 px tile (15 pixels at 8x), no smoothing, with a 16% black grid and the pixel under the pointer boxed;
  - the Overlay's dim and outline drawn at the same zoom;
  - a 10 px radius with a 2 px accent ring;
  - 24 px below-right of the pointer, each axis flipping at the edges;
  - a dark x, y pill 8 px below it in Geist Mono 12;
  - no color readout.
- **Thumbnail** (look variant A):
  - 240 × 150, 12 px corners, a soft shadow, a white-at-10% outline;
  - stacked 16 px from the bottom-left screen edges, 12 px apart;
  - hover: a 72% black scrim fading in over 100 ms, Copy / Save pills, and 28 px round ×, Pin and Edit buttons (Trim and a `GIF 0:06` chip on a Recording);
  - hover zones reach half the gap above and below, and faded controls are invisible to hover;
  - a new Thumbnail appears at once, and the others move up over 300 ms ease-in-out;
  - every exit is a 650 ms slide off the left edge on `cubic-bezier(0.25, 1, 0.5, 1)`, with a fade three-quarters as long on `cubic-bezier(0.25, 0.46, 0.45, 0.94)`;
  - Copy and Save show a check first (200 ms on `cubic-bezier(0.2, 0, 0, 1)`), and the slide starts 450 ms after the click;
  - pressing does nothing until release, which opens the Editor or Trim;
  - no drag-out.
- **Editor** (layout variant A):
  - a 52 px top toolbar: close, Crop, Add image, Background, the 12-tool capsule, the current tool's selects, Save as…, Done (accent);
  - a 52 px bottom bar: the zoom menu on the left, Pin and Copy on the right, nothing in the center;
  - a 284 px left Background panel that pushes the canvas over;
  - Crop mode swaps the toolbar;
  - the canvas has at least 40 px of margin and no hairline round the shot;
  - size steps, each opening on its third:

| Tool group | Steps |
|---|---|
| Line width | 2, 4, 6, 10, 16, 24 px |
| Text | 16, 22, 30, 40, 56, 80 pt |
| Counter | 11, 14, 20, 24, 31, 45 pt |
| Highlighter | 12, 18, 26, 36, 50, 70 px |

  - Opening size: the shot at 100% plus chrome and a 40 px margin, clamped between half the screen width × 56% of its height and two-thirds × 70%, never narrower than the toolbar needs (1180 px). It's computed from the monitor, never hard-coded, and must be right on first show, because Hyprland ignores later self-resizes.
- **Annotation Handles**:
  - the Overlay's dots, drawn at screen size above every Annotation;
  - boxes get eight dots plus a 3 px accent outline at 50%;
  - lines and arrows get one dot at each end;
  - Text, Counter, Pencil and Highlighter get a 3 px accent outline at 50%, 4 px out, with no dots;
  - a dot grows to 18 px under the pointer, with the matching resize pointer;
  - hover shows the selection's ghost at 40%.
- **Pin:** a floating window kept above every window on every workspace. Drag moves it, scroll resizes it, and Ctrl+scroll changes opacity. Middle-click, hover × or Esc closes it. Right-click offers Copy, Save as…, Edit and Close all pins, and Ctrl+C copies.
- **Record panel and Recording bar**:
  - The Record panel sits inside the kept Selection at its bottom center, or outside on a small area. It holds two rows:
    - W × H fields that resize the Selection live with the top-left fixed (Tab between them), and the settings button;
    - mic, system audio and Show clicks.
  - Below it sits the menu Record Video ↩ / Record GIF ⌥↩. Esc cancels, and the Selection's Handles still work.
  - The Recording bar has a red stop with red elapsed time in Geist Mono (plus a mic glyph when the mic is on), then 40 px cells for pause/resume, restart, delete and grip.
  - Placement is below the area, else above, right or left; there's no bar when nothing fits.
  - After a GIF stop, the bar springs narrower to a progress ring with "GIF 42%" and leaves. After an MP4 stop or a delete, it vanishes at once.
  - The area's outline turns red at once.
- **Ripple:** a ring growing from the press point to about 48 px and fading over about 400 ms, in the accent with a faint fill, the same for every button. It's tuned with `/animate`.
- **Countdown:** a small focus-free label in a corner of the chosen area.
- **Trim window** (look variant D):
  - the Editor's 52 px top bar: close; transport (to start, play/pause, to end); time `0:04.40 / 0:12.40`; Start here / End here; `Keeps 0:07.60`; Copy, Save as…, Done;
  - the preview on the Editor's canvas;
  - a 104 px bottom bar with a 52 px filmstrip in an accent frame, whose 14 px thick ends are the Handles, frames outside the kept range dimmed, a 3 px white Playhead, and about 10 or fewer labeled time ticks;
  - a dark time pill follows the pointer on the strip;
  - while trimming, the bottom bar becomes "Trimming… 62%" with a progress bar and Cancel;
  - the opening size follows the Editor's rule.
- **Motion rules for every surface:**
  - Keyboard-opened surfaces appear with no open animation.
  - Controls change state instantly: hover, press, pause ↔ play, toggles.
  - Motion is kept only where it carries meaning.

### Keys

| Global key | Does |
|---|---|
| Print | Overlay: area or window |
| Super+Print | Full screen (the monitor under the pointer) |
| Shift+Print | Repeat the last area |
| Ctrl+Print | Record: the Overlay, then the Record panel. Pressing it again stops |
| Ctrl+Shift+Print | Text capture |
| Super+Shift+Print | Timer: drag an area or press F. Pressing it again cancels |
| Super+Ctrl+Print | `tarn restore` |

- No Alt with Print, because Linux reads Alt+Print as SysRq.
- Keys inside Tarn's windows are fixed in v1, as listed in the user stories above.
- The Trim window's keys: Space, I / O, ← / →, Shift+← / →, Home / End, Enter, Ctrl+S (Save as…), Ctrl+C, Esc.

### Tarn's Hyprland file

Tarn ships one Lua file, `tarn.lua`, symlinked and loaded from the user's `hyprland.lua` the way `island.lua` is. It only adds and never unbinds. It holds:

- the global key binds, each running a `tarn` command;
- the daemon's start at login;
- a `no_anim` layer rule whose pattern matches the **whole** namespace. `^tarn(-.+)?$` works; `^tarn` silently matches nothing, and Hyprland then fades every Tarn surface in over about 250 ms;
- window rules for the Editor and the Trim window: `float`, `center`, `no_anim`. Without them Hyprland tiles the Editor;
- whatever rule keeps a Pin above every window on every workspace.

Other setup that sits outside `tarn.lua`:

- When the daemon is ready, Island's stopgap Print / Super+Print lines are deleted from `island.lua`. That's a change in Island's repo.
- The docs state the requirement for `cursor:no_hardware_cursors = 0`, now permanent in the user's config. Hyprland's software pointer is baked into both grim captures and Recordings, so while it's in use, "Show the pointer in Recordings" is grayed out with a note.

### Files and folders

| What | Where |
|---|---|
| Screenshots | `<Pictures>/Screenshots` (Setting) |
| Recordings | `<Videos>/Tarn` (Setting) |
| Settings, Presets | `~/.config/tarn/config.toml`, `~/.config/tarn/presets.toml` |
| Remembered state | `~/.local/state/tarn/state.toml` |
| History | `~/.local/share/tarn/history/` |
| Kept Recordings | `~/.cache/tarn/recordings/` |
| Copied files | `~/.cache/tarn/shared/` (swept after 1 day) |
| Island socket | `$XDG_RUNTIME_DIR/tarn/island.sock` |

- File names look like `2026-11-02_14-03-22.png`, or `…_firefox.png` for a window capture.
- The pattern is fixed in v1.

### Settings

| Setting | Default |
|---|---|
| Screenshot folder | `<Pictures>/Screenshots` |
| Recordings folder | `<Videos>/Tarn` |
| Wallpaper folder | `~/Pictures/Wallpapers` |
| Keep History for | 1 month |
| Text capture languages | English (setup offers French and Spanish) |
| Timer delay | 3 s |
| Show the pointer in Recordings | on (grayed out under a software pointer) |
| Copy automatically after a capture | off |
| Save automatically after a capture | off |
| Close the Thumbnail after | off; never for a Recording |

How Settings interact:

- **Auto-close doesn't save**, just like ×.
- **Automatic Copy or Save still shows the Thumbnail.**
- **What Copy and Save include:** whatever is copied or saved includes the auto-applied Preset's Frame.

### Island's part ([ADR 0002](../adr/0002-island-connects-to-tarn.md), [docs/shell-socket.md](../shell-socket.md))

- Island's side is separate work in Island's own repository. It covers:
  - the Recording kind;
  - a Recording always taking the Circle;
  - a fixed red `#FF453A` dot that turns into a stop square on hover;
  - paused and encoding states;
  - the click to stop, which mustn't dismiss an ended timer;
  - deleting the stopgap binds.
- The Timer's Countdown stays off Island in v1.

## Testing Decisions

- **One seam: the daemon's C++ core, just below QML.**
  - **Tests send in** the same requests the `tarn` commands send, plus the actions the QML screens call. For example: "Selection confirmed at this rectangle", "Copy pressed on Thumbnail 2", "Arrow drawn from A to B", "Trim Handles at 2.0 s and 9.6 s", "pointer pressed at x, y".
  - **Tests assert only on what's visible from outside:**
    - files in the folders, and their names;
    - History PNG and description pairs;
    - clipboard contents;
    - the outside commands run, with their arguments;
    - the JSON lines a stand-in Island client receives on the socket;
    - the remembered-state file;
    - notifications raised.
- **Outside programs are faked.** Stub executables are placed first on the `PATH` (`grim`, `tarn-clip`, `gpu-screen-recorder`, `gifski`, `ffmpeg`, `tesseract`, `hyprctl`, the notifier). Each one records its arguments and returns a fixture: grim returns a fixed PNG, the recorder writes a short sample MP4, tesseract prints sample text. The XDG folders point at a scratch directory, and Qt runs off-screen, so tests never touch the real screen, clipboard or home folder.
- **Editor drawing is tested through the same seam.** A test applies edits, then Copies or Saves, and compares the written PNG with a stored reference image. This covers:
  - the Pixelate rule (picture hidden, Annotations untouched);
  - irreversible pixelation;
  - Spotlights sharing one dim layer;
  - Counter renumbering;
  - non-destructive Crop;
  - rotate, flip and output scale;
  - Frame fractions on different shot sizes;
  - a canvas grown by an added image;
  - undo across Annotation, Crop and Frame changes.
- **What makes a good test here:** it describes what the user would notice (a file, a clipboard entry, a message to Island), never which internal class did the work. That way the Overlay model, the Capture document or the Recording session can be reshaped without rewriting tests.
- **Areas the tests must cover:**
  - Thumbnail stack rules: max 4, overflow auto-save with edits and Frame, save failures, restore, hidden-while-editing, auto-close never for Recordings;
  - file naming and clashes;
  - History writes, the sweep, and Trash on a shortened retention;
  - Settings: live reload, one bad line, written once;
  - the Recording session's states and the Island lines for each (including pause disabled until writing, restart, delete, GIF encoding progress at most 10 a second, and recovery after a crash);
  - Trim frame snapping and re-make;
  - Text capture's paragraph joining;
  - the Timer cancel;
  - the Preset skip;
  - Show clicks adding and removing its binds and drawing only inside the area;
  - the Overlay model's selection rules (window stickiness, let go versus Ctrl-let go, keyboard selection).
- **Not automated:** QML look and motion, real Hyprland behavior and real-speed feel. These are checked at the desk on the real monitor, marked **needs-eyes** in each build ticket's verification.
- **Prior art:** none in the codebase yet, since there's no code.

## Out of Scope

- Studio Mode, now planned as the Recording Editor (Recordings as editable projects: zooms, cursor styling, camera): its own later effort.
- **Show keystrokes:** it needs `/dev/input` access through a privileged helper. It's the first candidate after v1, and chord-only if it returns.
- Scrolling capture: the first candidate for its own effort.
- Cloud upload and hosted share links. "Share" means copy, save, or open in an app.
- **Drag-out anywhere in Tarn.** Copy and paste covers it.
- Hiding desktop icons, which Hyprland doesn't have.
- Packaging and distributing Tarn for other people, for now.
- Later, not v1:
  - a History window;
  - a Settings window;
  - window recording;
  - the converter's quality, dimensions and audio controls;
  - converting between GIF and MP4;
  - the Countdown on Island;
  - pause, restart or delete from Island;
  - Pin Lock Mode;
  - keys on the Thumbnail;
  - a list of recently closed Thumbnails;
  - curved arrows;
  - the Highlighter snapping to text;
  - Crop snapping to edges inside the picture;
  - saved colors;
  - inserting a new screenshot into the Editor;
  - a re-editable project file;
  - a second "keep every line break" text key;
  - QR codes and link detection;
  - text capture from the Editor or a Pin;
  - automatic language detection;
  - a Timer delay per shot;
  - a window Timer;
  - a file-name pattern Setting;
  - a default-format Setting;
  - rebinding keys inside Tarn's windows.

## Further Notes

- **Readings this spec took where the map was silent:**
  - In record mode the Overlay selects an area only, with no window highlight, like Timer mode, because "no window recording" was decided.
  - Text capture selects an area in the Overlay the same way.
  - The daemon starts from `tarn.lua`.
  - All of these are cheap to change.
- **If hardware pointers misbehave on the NVIDIA card,** another way to keep the pointer out of captures is still open (the map's only remaining fog). Today `cursor:no_hardware_cursors = 0` is permanent and working.
- **Quick checks the first build should make:**
  - that `startSystemMove()` drags the frameless Editor on Hyprland;
  - the exact `hyprctl eval` lines that add and remove Show clicks' binds;
  - that the click-through Ripple layer shows up in a region Recording.
- **gpu-screen-recorder:** its helper that reads the screen is set up by the Arch package with no password prompt. Tarn's docs should mention it.
- **Licenses:** gifski is AGPL and `wl-copy`, `tesseract` and `gpu-screen-recorder` are GPL, so they're only ever run as commands. Showmethekey (Apache-2.0) is the model if Show keystrokes returns.
