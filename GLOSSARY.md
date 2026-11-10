# Tarn

A screenshot and screen-recording tool for Hyprland. This glossary names the things a capture passes through and the things the user does to it.

## Language

### Framing

**Frame**:
The whole look placed around a shot for sharing: its background plus padding, inset, shadow, corners, alignment, ratio and any Watermark.
_Avoid_: Background (for the whole look), border, backdrop

**Background**:
The fill drawn behind the shot inside a frame: none, a gradient, a wallpaper, a blurred copy of the shot, or a plain color.
_Avoid_: Backdrop, canvas

**Preset**:
A saved, named frame that can be applied to any shot, and optionally to every capture automatically.
_Avoid_: Template, style, theme

**Watermark**:
A small image or line of text that a Frame places in one corner of the shot, at a set size and opacity.
_Avoid_: Logo, signature, stamp

**Palette**:
The desktop's colors, usually derived from the current wallpaper by the user's shell or a tool like matugen, which Tarn follows for its own screens and offers as palette gradients and swatches. Without one, Tarn uses its own colors and offers no palette gradients or swatches.
_Avoid_: Theme, color scheme

**Saved colors**:
The user's own row of up to eight colors, kept from the Editor or by a Color pick, offered wherever Tarn offers swatches: the color popover and the Frame's plain colors.
_Avoid_: Favorites, recent colors, custom colors (Custom is the picker)

### Capturing

**Overlay**:
The full-screen layer over a frozen image of the screen where the user picks an area or a window to capture.
_Avoid_: Selector, picker, crosshair (one part of it)

**Selection**:
The area or window picked in the Overlay. Letting go takes it at once; holding Ctrl while letting go keeps it on screen to move and resize before taking it.
_Avoid_: Region, rect, marquee

**Mode**:
What a Selection in the Overlay does: Screenshot, Record, Text or Color. One Mode is active at a time; the Timer and Sequence are toggles, not Modes.
_Avoid_: Tool, capture type

**Mode bar**:
The capsule of buttons at the top center of the Overlay: the four Modes, the Timer and Sequence toggles, Full screen and Last area, each button showing its key. It moves to the bottom edge while a Selection lies under it.
_Avoid_: Toolbar (the Editor's), dock, picker

**Sequence**:
Staying in the Overlay on the same frozen image after a shot, to take several Selections of content that must not change.
_Avoid_: Freeze mode (the Overlay is always frozen), burst

**Last area**:
The area most recently taken over a given app's window, which the user can bring back to take again.
_Avoid_: Previous region, retake

**Magnifier**:
The zoomed view of the pixels around the pointer in the Overlay, for placing an edge on an exact pixel. M turns it on and off, and it stays as the user left it for the next capture. It shows where the pointer is, not the pixel's color, except in the Color Mode, where it is always on and its pill reads the color a click would copy.
_Avoid_: Loupe, zoom, lens, color picker

**Text capture**:
Selecting an area or window of the screen to copy the text in it, rather than its image.
_Avoid_: OCR (the technique, not the act), text grab

**Text layout**:
How a Text capture arranges what it copies: Paragraphs (lines within a paragraph joined), Lines (every line break and its indentation kept) or Table (columns separated by tabs, for pasting into a spreadsheet). It stays as the user left it for the next Text capture. A QR code in the Selection replaces the text, whatever the Text layout.
_Avoid_: Mode (the Overlay's Modes), format, keep-line-breaks key

**Color pick**:
Copying the color of the pixel under the pointer in the Overlay. The color also joins the Saved colors.
_Avoid_: Eyedropper (the Editor's sampler of the shot), color picker (the Editor's color popover)

**Timer**:
A capture that waits a set delay after the user chooses what to capture, then takes the shot.
_Avoid_: Delay, self-timer

**Countdown**:
The Timer's seconds left, shown as a red pill at the top center of the monitor, under any top bar. While it runs, the area being captured stays outlined with the rest of the monitor dimmed (a full-screen Countdown has only the pill), and clicks reach the windows beneath. All of it leaves just before the grab.
_Avoid_: Delay, timer (the whole capture)

**Recording**:
A capture of an area over time, delivered as a video or a GIF.
_Avoid_: Screencast, clip, video capture

**Record panel**:
The small panel over a kept Selection where a Recording is set up and started, as a video or as a GIF.
_Avoid_: Record menu, pre-record popup

**Recording bar**:
The floating bar of controls for a running Recording: stop with the time so far, pause, restart and delete. It stays outside the recorded area, so it is never in the video. Exception: when it fits nowhere outside (a full-screen Recording, or an area nearly filling the monitor) it sits inside, at the monitor's bottom center, and auto-hides: shown when the recorder starts writing, hidden after 3 s, shown again while the pointer touches an invisible 2 px strip along the monitor's bottom edge (hidden 1 s after the pointer leaves the bar and the strip), and kept shown while paused. Only this auto-hiding bar moves: it rises and sinks like an on-screen display, on springs that bend when reversed; every other change of the bar is at once.
_Avoid_: Pill, HUD, toolbar, indicator

**Recording Editor**:
The window, not built yet, where a Recording is changed as an editable project before it is shared: zooms, pointer styling, keystrokes, audio and a Frame. It is to a Recording what the Editor is to a shot.
_Avoid_: Studio, Studio Mode, video editor

**Ripple**:
The ring drawn into a Recording where a mouse button was pressed inside the area, when the user chose to show clicks.
_Avoid_: Click effect, press effect, highlight

**Trim**:
Shortening a fresh recording by cutting time from its start or end.
_Avoid_: Cut, crop (crop is cutting an image's edges)

**Playhead**:
The line marking which moment of a Recording is showing while it is trimmed; setting the start or end "here" uses its moment.
_Avoid_: Cursor (the pointer), scrubber, needle

**History**:
The kept, untouched originals of recent image captures, each with its Description and any Project file, which expire together after a set time. Editing a capture never changes its original.
_Avoid_: Archive, recents, log

**History window**:
The window that lists History's entries, newest first by day, and acts on them: Edit, Pin, Copy, Save as, Restore, Revert to original and Delete. An entry with edits is shown and handed out as its finished picture.
_Avoid_: Gallery, library, browser

**Restore**:
Bringing a capture back as a Thumbnail, edits included, either the last one closed or chosen entries from the History window.
_Avoid_: Reopen, recover, undo

**Revert to original**:
Throwing away every edit made to a capture in History, so it is the untouched original again. The edits go to the Trash, not away for good.
_Avoid_: Restore (bringing back a Thumbnail), reset, discard

**Description**:
What Tarn knows about one capture in History: when and where it was taken, the app and window it shows, the text read from it, and where it was saved.
_Avoid_: Metadata, sidecar, info

**Project file**:
The edits made to one capture in History (its Annotations, Crop and Frame, but not its undo history), kept so it reopens in the Editor with every Annotation still movable. The finished, annotated picture is never kept; it is rebuilt from the original and its Project file.
_Avoid_: Project (alone), save file, edit file

### Editing

**Editor**:
The window where a capture is changed: annotated, cropped and framed.
_Avoid_: Annotate window, canvas (the area inside it)

**Annotation**:
One thing drawn or placed on the shot, such as an arrow, a box, some text or an added image, which stays movable until the capture leaves Tarn.
_Avoid_: Markup, drawing, shape, object

**Handle**:
A part of something that is dragged to reshape it: a dot on a selected Annotation or a kept Selection (to resize it, move one end of a line, or bend an arrow by its middle), or one end of the Trim frame (to move where a Recording starts or ends).
_Avoid_: Grip, knob, control point

**Crop**:
Cutting away the edges of a shot.
_Avoid_: Trim (cutting a recording's time), resize

**Hand**:
The Editor tool (G) that drags a shot zoomed past Fit around the view. It draws, selects and moves nothing, and it is offered only while the shot is larger than the view.
_Avoid_: Pan tool, grab tool, move tool (moving is what Select does to Annotations)

**Pixelate**:
An Annotation that hides the picture beneath it by pixelating, blurring or blacking it out.
_Avoid_: Redact, censor, blur (one of its styles)

**Spotlight**:
An Annotation that dims everything outside its box to draw the eye inside it.
_Avoid_: Highlight, focus

**Highlighter**:
A see-through marker stroke, like a highlighter pen on paper.
_Avoid_: Marker, highlight

**Counter**:
A numbered marker for a step, numbered in the order the counters were placed.
_Avoid_: Stamp, step, badge

**Callout**:
A box of text joined to a Counter by a line, pulled out of it with the Counter tool. The line runs from the Counter's edge to the box's edge and stays attached when either one moves.
_Avoid_: Label, note, tooltip, leader (for the whole; the leader is just its line)

### After capture

**Thumbnail**:
The small floating preview of a fresh capture, from which it is copied, saved, pinned or opened for editing.
_Avoid_: Quick Access, overlay, popup, toast

**Capture hide**:
The whole Thumbnail stack off screen while Tarn takes a shot or records, so Thumbnails are never in a capture. It lasts from before the shot (or the Overlay's frozen image) until the capture is on the stack, a cancel, or the recorder's exit, and the Thumbnails then spring back in from the left edge together. Not the same as a Thumbnail hidden while its Editor is open.
_Avoid_: Hide all, stack hidden (for the per-Thumbnail Editor hide)

**Pin**:
A capture floated above every window until the user closes it.
_Avoid_: Float, sticker, floating screenshot

**GIF Pin**:
A Pin of a GIF Recording, looping. It keeps the GIF as it was when pinned, whatever happens to the Recording afterwards.
_Avoid_: Animated pin, video pin

**Locked**:
A Pin that clicks and scrolling pass through to whatever is beneath it. It ignores the mouse and the keyboard until it is unlocked.
_Avoid_: Click-through, pass-through, ghost, frozen (Freeze is the Overlay's frozen screen)

### Tarn as a whole

**Shell socket**:
Tarn's connection for the user's shell or bar (Waybar, Quickshell or any other): while a Recording runs, Tarn tells whatever is connected its state, and the shell can stop it. Tarn behaves the same with nothing connected.
_Avoid_: IPC, bridge

**Setting**:
A preference the user chooses by editing Tarn's settings file, such as the screenshot folder or the Timer delay. What Tarn remembers from last use (the last area, the tool color, the mic toggle) is never a Setting.
_Avoid_: Preference, option, config (for a Setting); setting (for anything Tarn remembers on its own)

**File name pattern**:
The Setting that decides what a saved capture's file is called, written as words in braces (such as `{date}`, `{app}`, `{title}`) that each become part of the name.
_Avoid_: Template, naming scheme, format string

**Image format**:
The Setting that decides whether Save writes PNG, JPEG or WebP. It governs pictures only; a Recording's format is chosen per Recording.
_Avoid_: Default format, file type, export format

**GIF width limit**:
The Setting that caps how wide a GIF Recording's GIF is, in pixels: a wider one is made smaller to fit, keeping its shape. The Recording's video always keeps its full size. Off when set to 0.
_Avoid_: GIF quality, GIF size, max width
