# GuyDances — design

## Intent

A tiny desktop companion: a pixel-art guy who appears only while music is
playing on the computer, dances in place, can be grabbed and moved anywhere,
and otherwise stays out of the way. It should feel like a native little
creature, not an app window.

## The one rule that matters

He is not a music recognizer. He appears when a **known media app is actively
producing sound**, and leaves when it stops. No microphone, no audio content
analysis, no titles or genres. This is deliberately fuzzy: a YouTube talk
video will wake him. Imperfection is accepted.

Known false positives, accepted for v1: a meeting running **inside a browser
tab** looks exactly like YouTube to this rule. On Linux we ignore sink inputs
that report `media.role = communication`, which Chromium sets for WebRTC; other
browsers may not, and Windows has no equivalent metadata. Native Teams, Discord
and Slack are excluded by the app whitelist.

## Behaviour

- **Appear**: a whitelisted media app has been outputting non-silent audio for
  about 1.5 s. He appears on top, without stealing keyboard focus.
- **Dance**: the 17.28 s clip from `guydances.mp4` (288 frames at 16.67 fps),
  cut out of its black/blue background, repeating.
- **Leave**: about 2 s after that app stops producing sound (song ends, paused,
  tab closed).
- **Drag**: left-press and move anywhere on the sprite. Native window drag, so
  it is smooth. Position is remembered across launches.
- **First position**: bottom-right of the primary monitor, 32 px above the
  bottom edge.
- **Right-click menu**:
  - `Pause` / `Resume` — freezes the current frame, stays visible, and ignores
    music stopping while paused. Resume returns to the normal rules; if nothing
    is playing, he leaves.
  - `Stop ▸` — `1 hour`, `2 hours`, `4 hours`, `8 hours`. Hides him immediately
    and ignores music for that long. Then normal rules apply again.
  - `Quit` — ends the process.
- **While snoozed** he is invisible, so there is nothing to right-click. Running
  the app again cancels the snooze (single-instance handoff) instead of starting
  a second guy.
- **While hidden** (no music, or snoozed) there is no visible handle either;
  relaunching the app is the way back to the menu. With no tray icon this is the
  accepted cost of "that's all".
- **No other UI**: no tray icon, no settings window, no installer wizard, no
  autostart entry.

## Goals

- One small download per platform; no runtime (no Python, Node, .NET, Qt).
- Instantly readable as the dancing guy: crisp pixels, true transparency, no
  frame, no background rectangle.
- Zero friction: double-click, it runs; music starts, he dances.
- Tiny footprint at rest (invisible, no polling spikes, under 1 % CPU while
  dancing).

## Non-goals

- Distinguishing music from speech. Any audio from a media app counts.
- Beat detection or dancing in sync with the song.
- Fighting fullscreen games or exclusive-fullscreen video.
- Autostart at login, tray icon, settings screen, themes, multiple dancers.
- Wayland. The target Linux machine is Ubuntu GNOME on **X11**; always-on-top,
  frameless overlays are compositor-restricted on Wayland. Under Wayland the
  app exits with a clear one-line message instead of misbehaving silently.

## Anti-goals (would count as failure even if it works)

- A visible border, black box, or taskbar entry.
- Asking for microphone or screen-recording permission.
- Dancing to Teams, Discord, Slack, or notification chimes. (A meeting inside a
  browser tab is a documented exception, not a silent one.)
- A full-window invisible layer that eats clicks meant for other apps.
- A 40–150 MB Electron/Qt download.
- Stealing focus while typing when he appears.

## Constraints

- Linux x86_64 (Ubuntu GNOME, X11) and Windows 10/11 x64.
- No admin rights for the app itself; no system-wide install, no installer.
- Offline at runtime; no telemetry or network calls. Build-time downloads are
  fine.
- Windows binary is cross-compiled from Linux with MinGW-w64, so it must stick
  to APIs that MinGW can compile (Win32, WASAPI, no WinRT). It therefore cannot
  be runtime-verified on this machine; a real Windows box is needed for that.
- Build dependencies come from apt (`cmake`, `libpulse-dev`, `libx11-dev`,
  `libgl-dev`, `mingw-w64`) plus a vendored GLFW source tree. Installing them
  needs the machine's sudo password once.
- Display scale is handled as 1× device pixels on purpose. He is a desktop pet,
  not a document; the window is DPI-aware so he stays crisp, but does not grow
  on a 150 % display (a scale option can come later).

## Decisions already made

### Language and framework — my call

**C++17 + GLFW + OpenGL (one textured quad), custom-drawn pixel menu.**

- Bash: cannot create a transparent, frameless, always-on-top, draggable image
  window; it would end up shelling out to other programs.
- Qt / Electron / Tauri: 40–150 MB or a WebKit dependency; wrong for "as small
  as possible".
- Zig / Rust: also fine and produce small binaries, but the libraries needed
  are C (GLFW, libpulse, X11, Win32) and C++ reaches them with the least
  friction. C++ gives the same tiny result.
- GLFW supplies the two hard window bits for free on both platforms:
  transparent framebuffer and always-on-top floating, borderless.
- Expected size: roughly 1 MB Linux, 2–3 MB Windows, including the sprite.

### Detection — my call

Ask the OS which apps are making sound, not what the sound is.

- **Linux**: PulseAudio/PipeWire sink inputs via libpulse. Playing means an
  uncorked, unmuted sink input with non-zero volume from a whitelisted
  application, plus a non-silent peak.
- **Windows**: WASAPI session enumeration (`IAudioSessionManager2`) on the
  default render device; active session, peak above silence, process name in
  the whitelist.
- **Whitelist** (shared header): Chrome, Chromium, Edge, Firefox, Brave, Opera,
  Vivaldi, Spotify, VLC, mpv, Rhythmbox, GNOME Music, Windows Media Player.
  Browsers cover YouTube, which is the main use.
- **Matching** is a case-insensitive substring against several identity keys,
  never the display title: PulseAudio `application.name` and
  `application.process.binary` (covers Snap `snap.firefox.firefox` and Flatpak
  `org.mozilla.firefox`), Windows process image name and package family (covers
  Store installs such as Spotify). One stable key per platform would not survive
  Snap/Flatpak/Store packaging.
- Debounce: 1.5 s of sound to appear, 2 s of silence to leave. Short blips from
  non-whitelisted apps can never trigger him; a 0.5 s Teams chime is both the
  wrong process and too short.

### Sprite — my call

- Build-time extraction from `guydances.mp4` with a Python/Pillow tool; ffmpeg
  only at asset-generation time, not at runtime.
- Quantize to a small palette, flood-fill the black and blue background from
  the frame edges (so interior black, like the eye, survives), crop to the
  union bounding box (about 200×192), and encode as palette + run-length/delta
  frames.
- Embedded in the executable; decoded one frame at a time at runtime, so RAM
  stays flat instead of holding 288 RGBA frames.
- Drawn at 1× with nearest-neighbour filtering. The art is already the right
  size for a desktop creature.

### Menu and window — my call

- Menu is custom-rendered (rounded pixel panel, 8×8 bitmap font, hover
  highlight), so it matches the sprite instead of looking like a system menu.
- It lives in its own transparent always-on-top popup window anchored at the
  cursor, expanding sideways for the `Stop` submenu, clamped to the monitor
  work area. Closes on selection, click-outside, or Escape.
- The dancing window is tight to the sprite's bounding box, so there is no
  large invisible click-catching area.
- Native drag: `_NET_WM_MOVERESIZE` on X11, `WM_NCLBUTTONDOWN`/`HTCAPTION` on
  Windows.
- Hidden from taskbar and pager (`_NET_WM_STATE_SKIP_TASKBAR`,
  `_NET_WM_STATE_SKIP_PAGER`, `WS_EX_TOOLWINDOW`).
- Only the window position is persisted, in a tiny config file
  (`~/.config/guydances/state`, `%APPDATA%\GuyDances\state`). Pause is
  deliberately not persisted: after a restart he follows the normal "hidden
  until music" rule. A saved position is clamped onto a monitor that still
  exists.

## Structure

```
CMakeLists.txt
src/main.cpp            window, loop, input, drag, menu wiring
src/audio.h             shared "is music playing" interface + whitelist
src/audio_linux.cpp     libpulse sink-input poller
src/audio_windows.cpp   WASAPI session poller
src/sprite.cpp/.h       RLE decode, texture upload
src/menu.cpp/.h         popup window + pixel menu rendering
src/font8x8.h           public-domain 8x8 font
tools/extract_sprite.py asset pipeline (mp4 -> embedded RLE)
assets/dance.bin        generated, embedded at build time
build-linux.sh
build-windows.sh        MinGW-w64 cross build
```

## Verification

On the Linux machine:

- Start with no audio → no window, no taskbar entry, low CPU at rest.
- Play a YouTube video in the browser → he appears within ~2 s, dances at the
  right speed, stays above other windows, does not take focus.
- Pause/close the video → he disappears after ~2 s.
- Play a notification sound (`paplay`) and a Teams-like chime → he must not
  appear.
- Drag him around, quit, relaunch → he comes back where he was left.
- Right-click: Pause freezes; Resume continues; Stop → 1 hour hides him;
  Quit exits.
- Measure binary size and resting/dancing CPU.

On Windows: build the cross-compiled `.exe`, then check the same behaviours,
plus transparency against the desktop and over a maximized window. This needs
a real Windows 10/11 machine, which is not available here; until it is tested
there, the Windows build ships as an unverified preview and the design says so
plainly.

Build setup, once: `sudo apt install cmake libpulse-dev libx11-dev libgl-dev
mingw-w64`, plus a vendored GLFW source tree. `./build-linux.sh` produces
`dist/guydances`; `./build-windows.sh` produces `dist/guydances.exe`.

## Left to the builder

- Exact palette, crop padding, and loop-seam treatment if the 17 s restart
  looks jarring.
- Menu dimensions, colours, and spacing within the pixel-art language.
- Whitelist additions discovered during testing.
- Whether to offer an optional autostart entry later.
