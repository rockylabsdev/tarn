# Tarn v2: the all-in-one Overlay, the History window and re-editable captures

A delta on [Tarn v1: the screenshot and recording flow](spec-v1.md): only what v2 adds or changes. Everything not named here works as v1 specifies. This spec replaces v1's "Later, not v1" list: every item on it is now either in v2 or listed under **Out of Scope**.

## Problem Statement

v1 gives the user a daily capture loop on Hyprland: Overlay, Thumbnail, Editor, Frame, Pin, Recording, Text capture and a kept History. Using it every day shows where the loop still runs out:

- Each kind of capture has its own key, so the user has to remember five Print chords and can't change their mind once the Overlay is open. Taking several shots of the same moment means reopening the Overlay on a screen that has already changed. "Repeat the last area" remembers one area for everything, not the area they always take in a given app.
- History is a folder of untouched PNGs. Finding last Tuesday's shot means scrolling a file manager full of timestamps, with no way to search what was in the picture. An edited shot can't be reopened with its Annotations still movable: the edits are gone once the Editor closes.
- Documentation shots keep wanting pieces the Editor lacks: a numbered step with a note pulled out beside it, an arrow that bends around something, a logo or name in the corner of every shot, and the user's own colors kept between sessions.
- File names say when, and sometimes which app, but never which window. Saved files are always PNG.
- Text capture always joins lines, which ruins code, lists and tables. A QR code on screen can't be read at all.
- A Pin always catches the mouse, so a reference image floating over work gets in the way. A GIF can't be pinned.
- There's no color picker for the screen.

## Solution

v2 deepens the same loop without opening a new front. It adds one new window and makes the Overlay the single way in:

- **The all-in-one Overlay.** Print opens the Overlay with a **Mode bar**: Screenshot, Record, Text and Color Modes, plus Timer and Sequence toggles, Full screen and Last area. Every v1 key stays as a way to open it preset to a Mode. **Sequence** keeps the Overlay open on the same frozen image for several shots. **Last area** is remembered per app. **Color pick** copies the color under the pointer.
- **The History window** (Alt+Print): a grid of every kept image capture by day, filtered by kind and app and searched by the text in the pictures. From it the user can Edit, Pin, Copy, Save as, Restore, Revert to original and Delete.
- **Re-editable captures.** Each History entry can carry a **Project file** beside its untouched original, so a capture reopens in the Editor with every Annotation, the Crop and the Frame still editable.
- **Editor additions:** **Callouts** pulled out of a Counter, curved arrows bent by a middle Handle, a **Watermark** in the Frame, and **Saved colors**.
- **Smart naming:** a **File name pattern** with app and window-title tokens, and an **Image format** Setting for PNG, JPEG or WebP.
- **Text capture extensions:** a **Text layout** of Paragraphs, Lines or Table (pastes into spreadsheet cells), QR codes read in the same shot, and a "Link copied" notice.
- **Pins:** **Locked** Pins that let clicks through, with one key to lock or unlock them all, and **GIF Pins** that loop a GIF Recording.

## User Stories

### The all-in-one Overlay

1. As the user, I want Print to open the Overlay with a Mode bar, always on Screenshot, so that press-and-drag works exactly as before while every other capture is one key away.
2. As the user, I want four Modes, 1 Screenshot, 2 Record, 3 Text and 4 Color, chosen by key or by clicking the Mode bar, so that I can change my mind after opening the Overlay.
3. As the user, I want Ctrl+Print, Ctrl+Shift+Print and Super+Shift+Print to open the same Overlay preset to Record, Text and the Timer, so that my v1 muscle memory still works.
4. As the user, I want Ctrl+Print to still stop a Recording and Super+Shift+Print to still cancel a Timer, so that the keys I start with are the keys I stop with.
5. As the user, I want Super+Print (full screen) and Super+Ctrl+Print (`tarn restore`) unchanged, so that nothing I rely on moves.
6. As the user, I want a Selection I kept with Ctrl to stay when I switch Mode, and a kept area switched to Record to get the Record panel on that same area, so that I can decide what to do with an area after drawing it.
7. As the user, I want F to take the whole monitor in every Mode, so that full-screen text, color and recording work the same way.
8. As the user, I want Text Mode to copy the text of a window when I click it, as well as of an area I drag, so that I can grab a whole dialog's text in one click.
9. As the user, I want T to toggle the Timer on Screenshot (areas and full screen only), with the window highlight off while it's on, so that a timed shot works as in v1.
10. As the user, I want the Timer's delay shown in the size pill with a clock icon (`3 s   700 × 400`), so that I can see a Timer shot is armed.
11. As the user, I want the Timer and Sequence toggles to start off every time the Overlay opens (except the Timer for Super+Shift+Print), so that I never take an unexpected timed or repeated shot.
12. As the user, I want S to toggle Sequence, which keeps me in the Overlay on the same frozen image after each shot, so that I can take several Selections of content that must not change.
13. As the user, I want each Sequence shot to go to the Capture store at once, with its Thumbnail appearing when I leave, so that nothing is lost and the screen isn't cluttered while I work.
14. As the user, I want an "N taken" pill at the Mode bar's right end while Sequence is on, so that I know how many shots I've taken.
15. As the user, I want Esc in a Sequence to clear a kept Selection first and leave on the next press, keeping every shot taken, so that one stray Esc doesn't throw away my work.
16. As the user, I want Sequence to work with Screenshot, Text and Color, and to end when I start a Record or a Timer shot, so that modes needing the live screen get it.
17. As the user, I want Shift+Print to take the focused app's Last area, and to open the Overlay when that app has none, so that "the same shot again" means the shot I take in this app.
18. As the user, I want L in the Overlay to bring back the app's Last area as a kept Selection, so that I can adjust it before taking it.
19. As the user, I want an area's Last area filed under the app whose window it overlaps most, not the app that had focus, so that an area drawn over my browser belongs to the browser.
20. As the user, I want Color pick to copy the hex (`#1e1e2e`) on click and `rgb(30, 30, 46)` on Shift+click, from the frozen image, so that I can paste a color straight into CSS or a config.
21. As the user, I want the Magnifier always on in Color Mode, with its pill showing a swatch and the hex (`rgb(r, g, b)` while Shift is held), so that I can see the exact color a click would copy.
22. As the user, I want arrow keys to move the pointer 1 px and Enter to pick in Color Mode, so that I can hit an exact pixel.
23. As the user, I want a notification showing the copied color ("Copied #1e1e2e · saved"), so that I know what's on my clipboard.
24. As the user, I want the Overlay to close after a Color pick unless Sequence is on, so that a single pick is one gesture.
25. As the user, I want the Mode bar at the top center as one capsule, with each button showing its key in a small corner label and a tooltip naming it, so that I can learn the keys without hovering.
26. As the user, I want the Mode bar to jump to the bottom edge, with no animation, while a Selection lies under it, and never to move just because the pointer hovers it, so that it never hides what I'm selecting and is always clickable.
27. As the user, I want the Overlay and every Mode change to appear instantly with no fades, so that the keyboard-opened UI stays as snappy as v1.

### History window

28. As the user, I want Alt+Print (`tarn history`) to open the History window instantly, and the same key or Esc to close it, so that browsing my captures is one key away.
29. As the user, I want only one History window, brought forward if it's already open, so that I never juggle copies.
30. As the user, I want every image capture (area, window, full screen, Timer shots) listed newest first under day headings (Today, Yesterday, then dates), so that I can find a shot by when I took it.
31. As the user, I want Text captures and Recordings left out of History, so that the grid is only pictures I can act on.
32. As the user, I want a Kind filter (All · Area · Window · Full screen) and an app filter (most-used apps first, with counts) that combine, so that I can narrow to "window shots of Firefox".
33. As the user, I want each tile to show the picture with the app, time and window title under it, so that I can recognize a shot at a glance.
34. As the user, I want a small pencil mark on edited entries, so that I can tell which shots carry edits.
35. As the user, I want hovering a tile to show Copy, Pin and Edit buttons, so that the common actions are one click away.
36. As the user, I want Enter or double-click to Edit, Ctrl+C to Copy, P to Pin, Ctrl+S to Save as, R to Restore and Delete to send to the Trash, and every action in the right-click menu with Show in Files, so that I can work the window by keyboard or mouse.
37. As the user, I want arrow keys to move the selection through the grid, so that I can browse without the mouse.
38. As the user, I want Ctrl-click, Shift-click and Ctrl+A to select several entries, with only Delete and Restore working on a selection, so that bulk actions are possible but unambiguous.
39. As the user, I want Restore to bring up to 4 entries back as Thumbnails, following the stack's overflow rule and skipping entries already on the stack, so that I can pick shots back up to share.
40. As the user, I want every action on an edited entry to use the finished picture, so that Copy, Pin, Save as and Restore give me what I made, not the raw shot.
41. As the user, I want Edit to reopen the capture with every Annotation still movable, and closing that Editor to save the edits and take me back to the History window with no Thumbnail, so that fixing an old shot is a round trip.
42. As the user, I want Edit to bring forward an Editor that's already open on that entry, and Pin always to open a fresh Pin, so that I never get two Editors on one capture.
43. As the user, I want Delete to move the whole entry (picture, Description, Project file) to the Trash with no confirmation, so that cleaning up is quick and recoverable.
44. As the user, I want Revert to original in the right-click menu of edited entries, which sends only the edits to the Trash, so that I can get the real capture back.
45. As the user, I want the window to update live as I take captures, as the sweep removes entries and when I delete a Thumbnail, so that it never shows stale content.
46. As the user, I want the bottom bar to describe the selection (`app · window title · width × height · Edited`, `N selected`, or `Select a capture`) and hold the actions with Edit in accent, so that I always see what an action will act on.
47. As the user, I want `No captures yet` with a hint when History is empty, and `Nothing matches` with a `Show everything` button when filters match nothing, so that an empty grid always explains itself.
48. As the user, I want the History window to stay open after I act on an entry, so that I can act on several in turn.

### Search

49. As the user, I want to type in a search field in the History window's top bar (Ctrl+F or `/`) and have the grid narrow to captures whose text, Text annotations, window title or app match, so that I can find a shot by what was in it.
50. As the user, I want every typed word to have to match somewhere, ignoring case and matching parts of words, so that search is predictable.
51. As the user, I want Esc to clear a search before it closes the window, and the field to start empty each time, so that search never sticks around.
52. As the user, I want each capture's text read in the background right after I take it, at low priority and paused while I record, so that search is complete without slowing anything down.
53. As the user, I want the daemon to catch up on unread entries (v1 entries and missed reads) when it starts, so that old captures become searchable too.
54. As the user, I want the count to read `22 captures · reading 3` while a search waits on unread entries, with matches appearing live, so that I know results are still coming.
55. As the user, I want search to keep matching titles and apps, with a placeholder telling me to install tesseract, when tesseract isn't installed, so that search degrades instead of breaking.
56. As the user, I want `Nothing matches` to name my search, and `Show everything` to clear it, so that a failed search is easy to undo.

### Re-editable captures (Project file)

57. As the user, I want every Editor close that keeps edits (Done, Enter, Copy, Ctrl+S, Save as, Pin) to save the edits beside the History original, so that I can come back to any shot later.
58. As the user, I want Esc in the Editor never to write edits, so that throwing a session away still throws it away.
59. As the user, I want the auto-applied Preset saved as the capture's edits at capture time, so that History shows and hands out the framed shot the Thumbnail gave me.
60. As the user, I want reopening to restore every Annotation (Callouts, arrow bends and Pixelate settings included), the Crop and the Frame, starting at Fit on Rectangle with empty undo, so that the capture is as editable as when I left it.
61. As the user, I want the Frame kept exactly as it looked (the exact gradient and wallpaper file), with a blurred-shot fallback and a notification if that wallpaper is gone, so that an old shot doesn't change look when my wallpaper changes.
62. As the user, I want added images kept inside the edits, so that a moved or deleted source image doesn't break an old capture.
63. As the user, I want finishing with nothing on the picture to remove the edits and the Edited mark, so that "edited" always means something.
64. As the user, I want a broken edits file to show the original with one notification naming it, and Done to replace it, so that a bad file never blocks me.
65. As the user, I want expiry still counted from capture time, so that editing an old shot doesn't keep it forever (saving is how I keep it).
66. As the user, I want the sweep to skip entries open in the Editor or on the Thumbnail stack, taking them after they close, so that nothing vanishes while I'm using it.

### Callouts and curved arrows

67. As the user, I want to drag out of a placed Counter with the Counter tool to pull out a Callout, a text box joined by a leader line, so that I can annotate a numbered step with a note.
68. As the user, I want a drag that starts on empty picture to place a Counter and pull out its Callout in one move, so that a numbered note is one gesture.
69. As the user, I want the Callout box in the Counter's color, holding only my text, and following its Counter's color, renumbering and deletion, so that the note belongs to its step.
70. As the user, I want a short drag (under about 24 px past the Counter) to make nothing, and each Counter to have at most one Callout, so that clicks don't sprout empty boxes.
71. As the user, I want the leader to meet the box's edge on the line towards its center, redrawn whenever either end moves, so that it always looks attached.
72. As the user, I want to drag the Counter or the box to move it alone and drag the leader to move all three, so that I can lay out the note precisely.
73. As the user, I want Delete on the box to remove the box and leader and Delete on the Counter to remove all three, an empty box to disappear, and double-click or Enter to edit the text again, so that Callouts behave like the Text I know.
74. As the user, I want every selected arrow to show a middle Bend Handle that bends it into a smooth curve, with the head and tapered shaft following the curve, so that an arrow can point around things.
75. As the user, I want a bent arrow to keep its shape when I move an end, and to snap straight when I drag the middle back near the line or double-click it, so that curves are easy to adjust and easy to undo.
76. As the user, I want drawing to still make a straight arrow and the bend to work on every arrow style, so that curves add a step without changing the tool.

### Watermark

77. As the user, I want a Watermark as part of a Preset's Frame, either an image file or one line of text, so that every shot can carry my logo or name automatically.
78. As the user, I want it in any of the four corners of the shot (bottom-right by default), at a fixed margin, recomputed after a Crop, so that it sits right even on a Preset with Background None.
79. As the user, I want Size (3–25% of the shot's shorter side, default 8%) and Opacity (10–100%, default 60%) sliders, so that it's visible without dominating.
80. As the user, I want text Watermarks in the Editor's Text font, white with a soft dark shadow, or near-black with a soft light shadow where the picture under them is light, so that they read on any picture.
81. As the user, I want the Watermark drawn above every Annotation and the Spotlight dim, never covered by Pixelate, so that it always shows.
82. As the user, I want a Watermark section at the bottom of the Background panel (Off / Image / Text, a chooser or text field, a four-dot corner picker, sliders), each change one undo step, so that it's set where the rest of the Frame is.
83. As the user, I want the image kept by its path in the Presets file, so that replacing my logo file updates every future shot.
84. As the user, I want a missing image to skip the Watermark with one notification and never block a capture, so that a moved logo doesn't stop my work.
85. As the user, I want B to skip the whole auto-applied Preset, Watermark included, and otherwise the Watermark to go wherever the Frame goes, so that it's on every output or none.
85a. As the user, I want a Watermark button (W) in the Editor's top bar that puts my last Watermark on the shot or takes it off, one undo step, so that adding one isn't tied to a Preset.

### Saved colors

86. As the user, I want a row of up to 8 Saved colors in the Editor's color popover (after Palette) and the Frame panel's plain colors, the same list in both, so that my colors are always at hand.
87. As the user, I want + to save the current color (opacity included), and + in the Frame panel to save the Frame's plain color (grayed when it isn't one), so that keeping a color is one click.
88. As the user, I want every Color pick to save itself to the front of the row, but the Editor's Eyedropper not to, so that colors I take from the screen are kept while one-off matches aren't.
89. As the user, I want newest on the left, the oldest dropped past 8, and a re-saved color moved to the front, so that the row stays fresh without duplicates.
90. As the user, I want right-click on a saved swatch to remove it at once, so that tidying is instant.
91. As the user, I want Saved colors remembered between sessions, so that they survive a restart.

### Smart naming and format

92. As the user, I want a `file_name` Setting of brace tokens (`{date}`, `{time}`, `{app}`, `{title}`, `{kind}`), so that I choose how my files are named.
93. As the user, I want the default `{date}_{time}_{app}` now on every capture that has a window, area shots included, so that I can tell where a shot came from.
94. As the user, I want the pattern to name Save, auto-save, the Save as suggestion, kept Recordings and the Copy file, so that a pasted file is named like a saved one.
95. As the user, I want an empty token to drop with its separator, so that an area over bare desktop is just `2026-11-02_14-03-22.png`.
96. As the user, I want titles cleaned to safe characters, cut at 60 on a word boundary and stripped of a trailing app name (`Issue 72 · GitHub — Mozilla Firefox` → `Issue-72-GitHub`), so that names are readable and short.
97. As the user, I want an `image_format` Setting of `png`, `jpeg` or `webp` (quality 90) for Save, auto-save and Save as, with Copy and History staying PNG, so that I can save smaller files without losing a lossless original.
98. As the user, I want a bad pattern, a `/`, or an unknown format to get the usual per-line Settings error and fall back to the default, so that a typo never breaks saving.

### Text capture extensions

99. As the user, I want K in Text Mode to cycle the Text layout through Paragraphs, Lines and Table, shown on the Text button ("Text · Lines"), so that I can copy prose, code or a table correctly.
100. As the user, I want the Text layout remembered for the next Text capture, so that I don't have to press K every time I grab code.
101. As the user, I want Table to copy tab-separated columns that paste into spreadsheet cells, and to fall back to Lines with "No table found, copied lines" when it finds no columns, so that Table never leaves me with nothing.
102. As the user, I want every Text capture to read QR codes too, with a code's contents copied in place of the text and a "QR code copied" notification, so that I can grab a QR code's link from the screen.
103. As the user, I want several QR codes in one Selection copied one per line, so that none is lost.
104. As the user, I want "Link copied" as the notification title when the whole result is a single link, so that I know a URL is ready to paste.
105. As the user, I want Tarn never to open a link itself, so that a stranger's QR code can't send me anywhere.

### Pins

106. As the user, I want to Lock a Pin from its right-click menu or with L, so that a reference image can float over my work without catching the mouse.
107. As the user, I want a Locked Pin to ignore clicks, scroll and the hover ×, never take keyboard focus, and keep its position, size and opacity, so that it behaves like part of the screen.
108. As the user, I want a lock badge on a Locked Pin, so that I can tell why it isn't responding.
109. As the user, I want Super+Shift+P to lock every Pin, or unlock them all when every Pin is already locked, so that I always have a way back.
110. As the user, I want Close all pins on any unlocked Pin to close the locked ones too, so that clearing up is one action.
111. As the user, I want Pin on a GIF Recording's Thumbnail, leaving the Thumbnail in place, so that I can loop a GIF beside my work without losing the GIF.
112. As the user, I want a GIF Pin to open where the GIF was recorded, at full size, looping with no controls, so that it replays the moment in place.
113. As the user, I want a GIF Pin to move, resize, change opacity, Lock and close like an image Pin, with Copy (the GIF file), Save as (GIF) and Close all pins in its menu and no Edit, so that it fits what I already know.
114. As the user, I want a GIF Pin to keep its own copy, so that trimming or deleting the Recording later doesn't break it.

### Keys and setup

115. As the user, I want Alt+Print and Super+Shift+P shipped in `tarn.lua` with the other binds, so that v2 works after a reload.
116. As the user, I want the History window to float, center and open with no animation, like the Editor, so that it never tiles or fades.
117. As the user, I want `zxing-cpp` listed as a dependency beside `tesseract`, so that setup tells me what QR reading needs.

## Implementation Decisions

### Keys

| Global key | Does (v2) |
|---|---|
| Print | The Overlay on Screenshot |
| Ctrl+Print | The Overlay on Record. Pressing it again stops a Recording |
| Ctrl+Shift+Print | The Overlay on Text, in the last Text layout |
| Super+Shift+Print | The Overlay on Screenshot with the Timer on. Pressing it again cancels |
| Super+Print | Full screen (unchanged) |
| Shift+Print | The focused app's Last area; with none, the Overlay |
| Super+Ctrl+Print | `tarn restore` (unchanged) |
| **Alt+Print** | `tarn history`: the History window; again closes it |
| **Super+Shift+P** | `tarn pin lock-toggle`: lock every Pin, or unlock all when all are locked |

- **Alt+Print is bound by key code, not key symbol.** v1 avoided Alt with Print because Linux reads Alt+Print as SysRq: on a PC keyboard, holding Alt changes the Print key's symbol to `Sys_Req`, so a Hyprland bind on `ALT + Print` never fires. Bind it on the Print key's code (`code:107`) with Alt. The kernel's magic SysRq only acts on a third key pressed with Alt+SysRq, and this machine allows only "sync" (`kernel.sysrq = 16`), so Alt+Print alone reaches Hyprland. Check this at the desk first (see Further Notes).
- **Overlay keys:** 1–4 Modes; T Timer; S Sequence; F Full screen; L Last area; K Text layout (Text Mode only); M Magnifier and B Preset skip as in v1; arrows and Enter as in v1; Shift+click in Color Mode for rgb. Shift, Alt, Ctrl and Space keep their v1 drag meanings.
- **History window keys:** Enter or double-click Edit, Ctrl+C Copy, P Pin, Ctrl+S Save as, R Restore, Delete, Ctrl+A select all, arrows move through the grid, Ctrl+F or `/` search, Esc clears the search, then closes.
- **Editor keys added:** W Watermark on/off; G the Hand tool, shown only when zoomed past Fit, alongside Space+drag and middle-drag. A custom-color slider drag on a selection is one undo step.
- **Closed from outside:** a window Hyprland closes (Super+Q) takes its normal way out: the Editor as Done (edits kept), a Pin as Close, Trim as ×.
- **GIF encoding** runs `ffmpeg` and `gifski` under `nice -n 10`, so the desktop stays smooth while a GIF is made.
- **Pin keys:** L locks a focused Pin. A Locked Pin can't be focused, so it's unlocked only by Super+Shift+P.

### Modules (changes to v1's modules)

- **Hyprland bridge**:
  - takes one window snapshot (`hyprctl clients`, `monitors`, `activewindow`) before the Overlay opens. Windows are kept only if they're on the workspace on screen (the `visible` field doesn't mean that);
  - answers "which window does this rectangle overlap most", used for the Description's app and title, the file name and Last area. For full screen it's the focused window. Floating windows count, stacked by their order in the list;
  - sets and clears a window's `no_focus` with `hyprctl eval 'hl.dispatch(hl.dsp.window.set_prop({window="address:<addr>", prop="no_focus", value="1"}))'` / `value="0"` (Hyprland 0.56+ reads `hyprctl dispatch` arguments as Lua and refuses the old `dispatch setprop address:<addr> no_focus 1` form; checked on 0.57.1), for Locked Pins.
- **Overlay model**:
  - v1's area/record/timer/text modes become the **Mode** (Screenshot, Record, Text, Color), with Timer and Sequence as toggles. The Overlay opens with a starting Mode and toggles from the request (`tarn capture area|record|text|timer`);
  - Text Mode gains the window highlight (a click copies the window's text). Record and the Timer stay area-only, and the Timer turns the highlight off;
  - switching Mode keeps a kept Selection;
  - Sequence: a confirmed Selection hands its shot over and returns to selecting on the same frozen image. The model counts shots taken. Esc clears a kept Selection, then leaves. Starting a Record or a Timer shot ends the Sequence;
  - Color Mode: the Magnifier is forced on and its readout is the pixel color. A confirm in Color Mode is a pick (hex, or rgb with Shift), with arrows and Enter as the keyboard pick;
  - L restores the focused app's Last area as a kept Selection;
  - K cycles the Text layout and writes it to remembered state;
  - Mode bar placement: top center, or the bottom edge while a dragged or kept Selection lies within 24 px of the bar's spot. The pointer's hover position never moves it.
- **Capture store:** Sequence shots go in at once, and their Thumbnails are held until the Overlay closes, then pushed in order under the stack's normal overflow rule. Every image capture records app and title from the snapshot. The auto-applied Preset is written as the capture's Project file.
- **Thumbnail stack:** gains **Restore from History** (up to 4 entries, skipping those already on the stack, following overflow) and **Pin on a GIF Recording's Thumbnail** (the Thumbnail stays). Editors opened from History don't hide or add Thumbnails.
- **Capture document**:
  - **Callout:** belongs to one Counter (at most one each). It holds text and the box position, and takes its color, number and lifetime from its Counter. Deleting the Counter deletes it, and an empty box removes itself. The leader is derived: from the Counter's edge to where the line from the box center towards the Counter crosses the box's edge;
  - Counter tool gestures: a click places a Counter. A drag from a Counter without a Callout, or from empty picture (placing a Counter first), opens a Callout once the drag passes about 24 px beyond the Counter's edge. A drag on a Counter that already has a Callout moves it. Dragging the leader moves all three;
  - **Arrow bend:** an arrow stores a bend offset relative to its tail-to-head line, so moving an end keeps the shape. The middle **Bend Handle** sets it. Within about 8 px of the line, or on a double-click, the bend snaps to zero. The head direction and the shaft taper follow the curve, for every arrow style;
  - **Project file** serialize/load: every Annotation (with Callouts, bends, Pixelate style and strength, Counter order), the Crop (area, rotation, flip, output scale) and the Frame locked to its look. Added images and an image Watermark are PNG data inside it. It never holds undo history or view state. Loading starts at Fit, on Rectangle, with empty undo;
  - flattening (Copy, Save, Pin, Restore, History tiles) is unchanged, with the Watermark drawn last.
- **Frame and Preset store**:
  - the Frame gains an optional **Watermark**: `off` | `image` (path) | `text` (one line), a corner (default bottom-right), Size (3–25% of the shot's shorter side, default 8%) and Opacity (10–100%, default 60%);
  - placement is the chosen corner of the shot, inside the card, with a margin of 3% of the shot's shorter side measured inside any Inset band, recomputed after Crop. It is drawn above every Annotation and the Spotlight dim and clipped to the shot's corners. Pixelate never covers it. Text uses the Editor's Text font, white with a soft dark shadow;
  - the Presets file keeps the image by path. A missing or unreadable image skips the Watermark with one notification naming the file. Applying a Preset replaces the whole Frame;
  - "locked to its look" for the Project file: exact gradient colors, the actual wallpaper file path (falling back to a blurred copy of the shot with a notification if it's gone), and the Watermark's image as PNG data.
- **Storage and naming**:
  - the **File name pattern** replaces v1's fixed name for Save, auto-save, the Save as suggestion, kept Recordings and the Copy cache file. History keeps its internal ids;
  - tokens: `{date}` `2026-11-02`, `{time}` `14-03-22`, `{app}` (class, lowercased), `{title}` (case kept), `{kind}` `area`/`window`/`screen`/`recording`. An empty token drops with the separator next to it, and doubled separators collapse. Clashes still get `_2`, `_3`;
  - cleaning: letters (any script), digits, `.`, `-`, `_` stay; everything else becomes `-`, repeats collapse and ends are trimmed. The title is cut at 60 characters on a word boundary. A trailing ` — X`, ` - X` or ` | X` is dropped when X matches the app class (ignoring case and vendor prefixes like "Mozilla");
  - **Image format** picks PNG, JPEG or WebP (fixed quality 90) for Save, auto-save and the type Save as preselects. Copy and History originals stay PNG.
- **History store** ([ADR 0003](../adr/0003-history-is-files-beside-the-untouched-original.md)):
  - each entry is a group of files sharing one id: the untouched `.png` (never rewritten), its Description `.json`, and an optional Project file `.edit.json`. There is no index, and nothing points from one file to another;
  - the Description gains `app` and `title` on every image capture (left out when no window applies) and `text` (missing means not yet read, `""` means read with nothing found). Fields are only ever added, so v1 Descriptions are read as they are, with no version number and no migration;
  - Project file lifecycle: written on every Editor close that keeps edits and by the auto-applied Preset at capture time. Deleted when edits come to nothing. **Revert to original** sends it alone to the Trash. An unreadable one makes the entry show and hand out the original with one notification naming it, and on the next Done it goes to the Trash before the new one is written;
  - expiry counts from capture time. Delete and the expiry Trash move the whole group. The sweep skips entries open in an Editor or on the stack and takes them on its first run after they close;
  - it emits change events (entry added, changed, removed, text read) that the History window listens to.
- **Text capture**:
  - **Text layout**: Paragraphs (v1's join), Lines (every line break and its indentation kept), Table. Paragraphs uses the default Tesseract call. Lines and Table run `tesseract stdin stdout -l <langs> --psm 6 tsv`. Lines groups the word boxes into rows like Table, indents each line by its offset over the character width (as spaces), spaces words by their gap on monospaced text (one space otherwise) and puts one blank line only where the gap is over 1.5x the median line pitch. Table regroups words into rows by vertical center (ignoring Tesseract's block and line ids), and cuts columns wherever a vertical strip is empty in at least 90% of rows. Cells are tab-separated and rows newline-separated. With fewer than two columns it copies Lines (from the same word boxes) instead and notifies "No table found, copied lines";
  - **QR:** `ZXingReader` runs on every Text capture's image, in every layout. If it finds codes, their contents (one per line) are copied in place of the OCR text and the notification reads "QR code copied" with the contents;
  - **Link notice:** when the whole copied result is one URL, the notification title is "Link copied" and the URL is the body. Tarn never opens links;
  - otherwise v1's notifications are unchanged: the first line, "No text found", or a failure.
- **Text reader** (new, beside Text capture): reads each new History original in the background with the `text_languages` Setting and writes the Description's `text`. It works one read at a time under `nice`, is paused while a Recording runs, and catches up at daemon start on every Description without `text`. A failed read leaves `text` missing until the next start. If `tesseract` isn't installed it reads nothing. It reads originals only, and edits never trigger a re-read. Entries already read aren't re-read when the languages change.
- **History window** (new):
  - a core list model the QML grid binds to. It reads every Description and Project file once when the window opens, then follows the History store's events, never the disk;
  - groups by day (Today, Yesterday, `Sunday 1 November`), newest first. Filters: Kind (All · Area · Window · Full screen) and app (from Descriptions, most used first, with counts), combined with search;
  - **search** matches the recognized `text`, the text of Text annotations and Callouts (read from the Project file, never OCR'd), the title and the app. Case is ignored, every word must appear, a word matches inside a longer word, and nothing is fuzzy. With a search typed and unread entries left, the count reads `N captures · reading M`. Without tesseract, the field's placeholder reads "Install tesseract to search text in pictures";
  - actions on the finished picture: Edit, Copy, Pin, Save as, Restore (selection, at most 4), Delete (selection), Show in Files, Revert to original (edited entries only). Single-entry actions are grayed on a multi-selection;
  - one window at a time: `tarn history` opens it, brings it forward, or closes it if it's focused. Edit opens the Editor on the rebuilt document. Closing that Editor writes the Project file and returns focus to the History window, with no Thumbnail;
  - look: variant A of the look study. 52 px top and bottom bars; the Kind capsule, app pill, search field and count on top; tiles at least 200 px wide with 14 px gaps under 15 px semibold day headings; a pencil disc for Edited; a 2 px accent ring for selected; Copy, Pin and Edit on hover; the selection summary and actions (Edit in accent) at the bottom. Empty and no-match states are as in the stories above. It opens at a fixed starting size (the study used 1200 × 780).
- **Pin**:
  - **Locked:** set by L or the menu, and toggled for all Pins by `tarn pin lock-toggle` (any unlocked → lock all; all locked → unlock all). Lock uses the Hyprland bridge's `no_focus` setprop: Hyprland's hit-testing skips such windows, so input reaches whatever is beneath. Qt's input-transparent flags and masks don't work on Hyprland (it picks a toplevel by its bounding box), so no part of a Locked Pin stays live. A lock badge the size of the × sits top-left. Close all pins closes Locked ones too;
  - **GIF Pin:** made from a GIF Recording's Thumbnail. It copies the GIF into its own cache file, discarded on close, and opens at the Recording's screen position at full size, looping with no controls. It plays with `AnimatedImage { cache: false }` (the default caches every frame: about 740 MB for a 10 s 1280×720 GIF). It has image-Pin move, scroll-resize, Ctrl+scroll opacity and Lock. Its menu is Copy (the GIF file), Save as (GIF) and Close all pins. No Edit.
- **Recording bar**: when the bar fits nowhere outside the recorded area (full screen, or an area nearly filling the monitor), it sits inside, at the monitor's bottom center, and auto-hides: shown when the recorder starts writing, hidden after 3 s, shown again while the pointer touches a 2 px strip along the bottom edge (hidden 1 s after it leaves the bar and the strip), and kept shown while paused. With room outside, v1's rule is unchanged.
- **Countdown:** the Timer's Countdown is a red pill (Island's recording red `#FF453A`, white clock and seconds) at the top center, 12 px below Island's resting pill (68 px from the top: Island sits 12 px down and is 44 px tall). The area being captured stays outlined with the rest of the monitor dimmed, as in the Overlay, and the whole Countdown is input-transparent so the user can set up the shot. A full-screen Countdown shows only the pill. All of it leaves at once just before the grab, so none of it is in the shot.
- **Settings:** adds `file_name` (default `{date}_{time}_{app}`) and `image_format` (default `png`), both written with comments when the file is created. An unknown token, a `/` in the pattern or an unknown format gets the usual per-line error and the default.
- **Remembered state:** adds per-app Last areas (keyed by app class, screen coordinates), the Text layout, and the Saved colors (up to 8, RGB plus opacity, newest first). v1's single last area is dropped and no longer read.
- **Editor screens**:
  - the color popover's rows become Fixed, Palette (when Island runs), **Saved**, Custom. The Frame panel's Plain color section ends with Saved;
  - + saves the current color (in the Frame panel, the Frame's plain color, grayed when it isn't plain). Right-click removes, with the tooltip "Right-click to remove". When the row is empty it shows only +;
  - Saved colors rules: newest first, at most 8, the oldest dropped, an equal color (same RGB and opacity) moved to the front. Color pick adds; the Eyedropper doesn't;
  - the Background panel gains the Watermark section: Off / Image / Text, an image chooser opening in Pictures or a text field, a four-dot corner picker, and the Size and Opacity sliders. Each change is one undo step.
- **Notifications:** v2 adds Color pick ("Copied #1e1e2e · saved"), "QR code copied", "Link copied", "No table found, copied lines", a missing Watermark image, a missing locked wallpaper, and an unreadable Project file. Copy and Save still raise none.

### Tarn's Hyprland file

- Adds the Alt+Print bind (by key code, see Keys) for `tarn history` and Super+Shift+P for `tarn pin lock-toggle`.
- Adds window rules for the History window: `float`, `center`, `no_anim`, like the Editor's.
- GIF Pins use the existing Pin rule. Lock needs no static rule: it's set per window at run time.
- Still only adds, never unbinds.

### Dependencies

- **`zxing-cpp`** (`ZXingReader`, Apache-2.0, in Arch `extra`): QR reading, about 29 ms on a 2560×1440 PNG. It's run as a command like the others. `zbar` was slower and missed a small blurry code.
- `tesseract` is unchanged, except for the extra `--psm 6 tsv` call for Table.

## Testing Decisions

- **One seam, unchanged from v1: the daemon's C++ core, just below QML**, driven through the existing `TestEnv` fixture with stub programs on `PATH`, scratch XDG folders and Qt offscreen.
- **Tests send in** the requests the `tarn` commands send and the actions QML calls. v2 adds:
  - Overlay Mode keys, toggles, L, K, and Shift+click in Color Mode;
  - `tarn history` and the History window's actions, filters and search text;
  - Editor edits for Callouts, Bend Handles, the Watermark and Saved colors;
  - Pin L and `tarn pin lock-toggle`, and Pin on a GIF Thumbnail.
- **Tests assert only on what's visible from outside**, v1's list plus:
  - the Project file `.edit.json` and the Description's `app`, `title` and `text` on disk;
  - **what the History window lists**: entries, order, day groups, counts (`· reading 3`) and Edited marks, read from the list model QML binds to. This is the window's one shaped output that isn't pixels, and it counts as outside like the clipboard;
  - Hyprland calls from the `hyprctl` stub's log (for example `setprop … no_focus 1`);
  - remembered state: per-app Last areas, the Text layout and the Saved colors;
  - notifications raised.
- **Stub changes:**
  - a new `ZXingReader` stub returns a fixture code or nothing;
  - the `tesseract` stub gains a `tsv` fixture answering `--psm 6 tsv`, so the Table rebuild runs on canned word boxes (a web table, `ls -l`, prose that must fall back to Lines);
  - `hyprctl clients` fixtures (overlapping windows, a floating window, another workspace, bare desktop) drive the snapshot behind `app`, `title`, file names and Last area.
- **Drawing** is tested as in v1: apply edits, Copy, and compare the PNG with a stored reference. This covers Callout leaders against box edges, curved arrows in every style with the taper following the curve, and the Watermark in each corner, at its opacity, above the Spotlight dim, never under Pixelate, and after a Crop.
- **The Project file round trip** guards everything it stores: edit, close, reopen from History, Copy. The PNG must match the one from before closing. The same holds for a Frame whose wallpaper file was deleted (the blurred fallback plus a notification).
- **Areas the tests must cover:**
  - the Overlay model: Mode switches keeping a kept Selection; Sequence counting, Esc order and ending on Record or Timer; Last area filing by largest overlap, Shift+Print with no area for the app, L; Color pick hex/rgb, keyboard pick and auto-save to Saved colors; Mode bar placement rules (Selection under the bar, hover never moving it);
  - Sequence Thumbnails appearing only on leaving, in order, with overflow;
  - History store: Description fields on every kind of capture, v1 Descriptions read unchanged, the Project file written, deleted and reverted, an unreadable Project file, the sweep skipping open entries, Delete moving the whole group;
  - Text reader: eager reads, catch-up at start, pause during a Recording, one at a time, a failed read retried at the next start, no tesseract;
  - History window model: grouping, both filters, search matching (every word, case, partial words, Text annotations and Callouts), the reading count, live updates on capture, sweep and Thumbnail Delete, Restore's limit of 4 and skipping, actions on edited entries using the finished picture, the Editor round trip with no Thumbnail;
  - Callout rules (24 px threshold, one per Counter, renumbering, deletion cascade, empty box removal) and arrow bend (shape kept when an end moves, 8 px snap, double-click straighten);
  - Watermark: the Preset skip, a missing image, Preset replace;
  - naming: every token, empty-token collapse, cleaning, the 60-character cut, the trailing-app trim, clashes, `/` rejected; image format for Save, auto-save and Save as, with Copy and History staying PNG;
  - Text layout: Paragraphs, Lines, Table and its fallback; QR replacing text; several codes; the Link copied rule;
  - Pin lock: the `setprop` calls, lock-toggle's all-or-none rule, Close all pins including Locked; GIF Pin from GIF Thumbnails only, the Thumbnail staying, the snapshot surviving a Trim or Delete of the Recording and being discarded on close;
  - Settings: the two new keys, defaults, and the per-line error.
- **QML tests** keep v1's light role: a screen (the History window, the Mode bar, the Watermark section) loads and binds without errors (`*_ui` areas with `LIBS tarn_ui`). Behavior is asserted in the core.
- **Not automated (needs-eyes):** the Mode bar's jump to the bottom edge while dragging across it; the History window's opening size and tiles across on the real monitor; hover buttons and the right-click menu with a real pointer; GIF Pin smoothness; and whether a Locked Pin really passes clicks through on live Hyprland (the `setprop` call is tested, Hyprland honoring it isn't).
- **Prior art:** `tests/text` (Tesseract stub, async `TextCapture::busy()` waits), `tests/history` (PNG and Description pairs, the sweep), `tests/overlay`, `tests/overlay_keys` and `tests/overlay_kept` (Selection rules), `tests/counter`, `tests/shapes` and `tests/pixelate` (reference-image drawing), `tests/presets` and `tests/frame` (the Presets file and Frame), `tests/pin` and `tests/pin_ui`, `tests/state` and `tests/settings`, `tests/stack` (overflow and restore).

## Out of Scope

- **The Recording Editor** (earlier called Studio Mode) and **scrolling capture**: separate efforts, planned after v1.
- **Converter controls, GIF↔MP4, and Island additions** (the Countdown on Island; pause, restart and delete from Island): recording-side work passed on to the Recording Editor.
- **Show keystrokes:** it needs a privileged `/dev/input` helper; held until the Recording Editor gives it a reason.
- **A Settings window:** the file is enough for about a dozen values.
- **Translate-on-capture** and **automatic language detection:** they need a model or a service.
- **Window recording** and a **window Timer:** the interesting version, follow-the-window, wants its own look at Hyprland's IPC.
- **Window-chrome Frame:** nothing depends on it.
- **Recordings in History:** they're large, kept in cache under other rules, and the Trim window owns them.
- **A details panel in the History window** (variant B): it can come back once a capture's details matter.
- **Small conveniences with no shared machinery:** keys on the Thumbnail, recently closed Thumbnails, the Highlighter snapping to text, Crop snapping inside the picture, inserting a screenshot into the Editor, rebinding keys inside Tarn's windows, a per-shot Timer delay, text capture from the Editor or a Pin, an Open button on a link notification, typed size and aspect-ratio lock for screenshots.
- **In the History window:** no Keep or star, expiry display, Clear History, rename, or Description editing. There is no "Edited" filter, no sort control and no search highlight or snippet.
- **As in v1:** cloud upload and share links, drag-out, hiding desktop icons, packaging for other people.

## Further Notes

- **Readings this spec took where the map was silent** (all cheap to change):
  - T is grayed and ignored outside Screenshot Mode, and switching to another Mode turns the Timer off;
  - each shot in a Text or Color Sequence copies and notifies on its own, so the clipboard holds the last one;
  - K's key hint sits in the Text button's tooltip, and the corner label keeps showing 3;
  - `tarn history` closes the window only when it's already focused, and otherwise brings it forward;
  - Callouts' text joins search along with Text annotations.
- **Quick checks the first build should make:**
  - that the Alt+Print bind by key code fires on this keyboard (`wev` at the desk), and doesn't trigger a kernel SysRq action;
  - that `setprop … no_focus 1` on a Pin passes clicks through on live Hyprland, and that `0` brings them back;
  - which window `hyprctl clients` reports on a special workspace (scratchpad), which was read from source only.
- **Table accuracy is best-effort.** Columns came out right on a web table, `ls -l` and `df -h` but failed on `ps aux` (tight right-aligned columns) and on a bordered spreadsheet grid, and OCR typos in small monospace digits are common. The sample was small and synthetic. Table adds about 0.2–1.1 s.
- **OCR cost:** about 1.3 s per full screen and 0.3 s per typical area shot, so a month of History reads in minutes at low priority.
- **Licenses:** `zxing-cpp` is Apache-2.0 and run as a command. Everything else is as in v1.
- **Glossary terms settled while planning** ([GLOSSARY.md](../../GLOSSARY.md)): Mode, Mode bar, Sequence, Last area, Color pick, Saved colors, History window, Restore, Revert to original, Description, Project file, Callout, Handle (with the Bend Handle), Watermark, File name pattern, Image format, Text layout, GIF Pin, Locked.
