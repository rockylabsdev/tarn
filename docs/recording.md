# Recording facts: gpu-screen-recorder, gifski, ffmpeg

Measured on the target machine (RTX 4090, 2560x1440, gpu-screen-recorder 6.1.5, gifski 1.34, ffmpeg 9) during the v1 prototypes and build. The code (`src/core/recording/`, `src/core/trim/`) shows the arguments; this file holds why they are what they are.

## gpu-screen-recorder

- An area is `-w region -region WxH+X+Y` (6.1.5's `--help`; the shorter `-w WxH+X+Y` some notes mention is wrong for this version). Whole monitor: `-w <monitor name>`.
- It captures through `gsr-kms-server`, which the package grants `cap_sys_admin`: no permission prompt, works on the NVIDIA card.
- **Start-up is slow:** 413–443 ms from launch to the first recorded frame (about 300 ms of it is NVENC starting), and the file appears at ~460 ms. So the recorder starts on confirm, and the Recording bar shows only once the file exists.
- **Stop with SIGINT**, not QProcess's `terminate()` (SIGTERM). The file is finished ~210 ms after SIGINT; the last frame is 60–72 ms before it.
- **Pause and resume: SIGUSR2 toggles.** SIGUSR2 sent before the output file exists kills the recorder.
- Steady 60 fps (median frame gap 17 ms, worst 33 ms); a keyframe about every 2 s.
- A Recording meant to become a GIF is recorded with `-q ultra` (constant-quality top preset). At the default, the h264 blocks show in the GIF, which copies them faithfully.
- **`-fallback-cpu-encoding yes` is always passed.** On a machine with no hardware encoder the recorder then encodes on the CPU instead of refusing to start. The flag does nothing where an encoder exists, so Tarn does not probe the machine per Recording. The fallback supports only h264, which is the codec Tarn already asks for (`-k h264`). The recorder itself is not niced: it records in real time. A different start-up time on the CPU changes nothing, because the Recording bar waits for the output file anyway.
- Every number in this file comes from an RTX 4090 with NVENC. On a machine without one, start-up, CPU load and encode times differ.

## GIF (gifski)

Frames go ffmpeg → y4m → gifski on stdin, at `--quality 100`.

- **Below 100 gifski dithers dark flat areas into blotches.** That is what quality 90 looked like in v1. Quality 100 costs ~30% more file size: a 15 s 846x272 clip is about 0.9 MB and 2 s to encode.
- `--extra` is 50% slower for no measurable gain. `--motion-quality` and `--lossy-quality` are already at their maximum at quality 100. 4:4:4 y4m is no better.
- PNG frames instead of y4m skip gifski's own YUV conversion (+8–10 dB PSNR), but the difference is invisible at normal size and costs a temp folder of frames. y4m stays.
- Encode time at 1280x800 is roughly 0.6 s per second of footage, so a long GIF needs visible progress and Cancel.
- **ffmpeg and gifski run under `nice -n 10`**. gifski takes every core, so at normal priority the desktop stuttered while a GIF was made. On an idle machine the GIF is no slower. `nice` execs the program, so Stop and Cancel still signal the encoder itself. Text capture's background reading uses `nice -n 19`: nobody waits for it.
- **The `gif_max_width` Setting (default 1280, `0` off) limits the GIF's width.** The ffmpeg step scales a wider video down with `scale='min(iw,N)':-2`: it never scales up, keeps the aspect ratio, and `-2` rounds the height to even, which y4m needs. gifski is told the same resulting width, or it would cap the animation at its own ~800x600 default. The width counts pixels, so on a 2x monitor a 1280-wide GIF is half of the Selection's pixels. The kept MP4 is always full size. Trim remakes a GIF to the same limit, still at `--quality 100` and 20 fps. A GIF's cost grows with its pixel count, so the limit is what keeps a full-screen GIF from taking minutes to make and tens of MB per quarter-minute.

| Busy 15 s 1280x800 clip, 20 fps | Time | Size | SSIM |
|---|---|---|---|
| gifski q90 | 9.1 s | 41.6 MB | 0.968 |
| gifski q70 | 6.0 s | 30.3 MB | 0.968 (blotchy) |
| ffmpeg palettegen `stats_mode=full`, bayer 3 | 4.4 s | 55.2 MB | 0.957 |
| ffmpeg per-frame palette | 27.3 s | 105 MB | 0.947 |

## Trim (ffmpeg)

Trim remakes the MP4 with CPU x264, `-preset veryfast -crf 20`, frame-exact, no audio. A GIF Recording is trimmed by remaking the GIF from the trimmed video.

| | 10 s 1280x800 | 50 s 1280x800 | 10 s 2560x1440 | 50 s 2560x1440 |
|---|---|---|---|---|
| x264 veryfast crf 20 (chosen) | 0.76 s, 2.4 MB | 3.4 s, 13 MB | 1.7 s, 2.8 MB | 7.7 s, 14 MB |
| NVENC p4 vbr cq25 | 0.94 s, 9.2 MB | 3.4 s, 46 MB | 2.6 s, 15 MB | 10.6 s, 78 MB |

Stream copy (`-c copy`) is near-instant but cuts only on keyframes (every ~2 s), so it is not frame-exact.

## Probing

```sh
ffprobe -v error -select_streams v -show_entries stream=width,height,r_frame_rate,duration -of json f.mp4
ffprobe -v error -select_streams v -show_entries frame=pts_time -of csv=p=0 f.mp4                      # frame times
ffprobe -v error -select_streams v -skip_frame nokey -show_entries frame=pts_time -of csv=p=0 f.mp4    # keyframes
```

ffprobe, ffmpeg and gifski can take seconds, so the daemon runs them with `ProcessRunner::runAsync`; `scripts/check.sh` fails on a blocking `run()` of them. `tarn doctor` is the exception: it runs in the client, which blocks only itself.

## The encoder probe (`tarn doctor`)

`gpu-screen-recorder --info` lists encoders under `section=video_codecs`: `h264` when the GPU encodes it, `h264_software` when the CPU can. The list is ready at once, but the portal step after it blocks for about 25 s when a portal request goes unanswered, ignoring SIGTERM, and on a pipe the list sits in stdio's buffer until the program exits. So `tarn doctor` runs it under `stdbuf -oL`, reads up to the end of the list and kills it (SIGKILL), which takes under a second. Within its 5 s budget it reports "couldn't tell" only if the list never arrives.
