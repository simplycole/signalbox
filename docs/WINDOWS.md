# Windows build and runtime guide

Signalbox builds as a native x64 Windows program from an **MSYS2 UCRT64**
shell. The Windows Terminal TUI uses the same retained renderer as macOS and
Linux, compiled against PDCursesMod's WinCon backend. Keyboard and resize input
come from Signalbox's native `ReadConsoleInputW` adapter.

Windows support is implemented and has been exercised on Windows 11, but it is
not yet a packaged release target. A clean-machine release-candidate pass,
dependency bundle, Windows CI job, and Credential Manager backend remain open.

## Requirements

Install current MSYS2, open the **UCRT64** shell, and install one ABI-consistent
dependency set:

```sh
pacman -Syu
pacman -S --needed make \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-curl \
  mingw-w64-ucrt-x86_64-json-c \
  mingw-w64-ucrt-x86_64-libgcrypt \
  mingw-w64-ucrt-x86_64-libao \
  mingw-w64-ucrt-x86_64-pdcurses
```

Do not mix MSYS, MINGW64, CLANG64, or third-party DLLs into this UCRT64 build.
GCC supplies the winpthreads dependency used by the existing thread model.

## Build and test

From the repository root in the UCRT64 shell:

```sh
make clean
make
make test
./signalbox.exe --help
./signalbox.exe --version
```

The default Windows curses backend is PDCursesMod WinCon. The Makefile checks
for `/ucrt64/include/pdcurses.h` and the selected static archive
`/ucrt64/lib/libpdcurses_wincon.a`. An optional VT output build exists only for
backend diagnosis:

```sh
make clean
make WINDOWS_CURSES_BACKEND=vt
```

Both builds keep native Win32 input. The unqualified PDCursesMod archive is the
WinGUI port and is not the intended Signalbox backend.

## Run

Use Windows Terminal for the supported interactive path:

```sh
./signalbox.exe
./signalbox.exe --tui
./signalbox.exe --classic
```

An interactive console selects the TUI automatically. `--classic` forces the
line interface. Redirected handles do not enter curses, and explicit `--tui`
fails with an actionable message when no interactive console is available.

The WinCon renderer writes Unicode through the console APIs and restores the
original screen buffer, modes, code pages, cursor, and input state on normal
shutdown. Signalbox owns key decoding, including arrows, Shift+Tab, Page
Up/Down, Home/End, Enter, Escape, UTF-16 surrogate pairs, and resize events.

## TLS and portable `cert.pem`

The MSYS2 development environment normally supplies a usable CA bundle. A
portable runtime must also be able to validate Pandora's TLS certificate.

If `ca_bundle` is set in the config, that path takes precedence. Otherwise,
the Windows build looks for `cert.pem` beside `signalbox.exe` and passes it to
libcurl. A portable directory should therefore contain at least:

```text
signalbox.exe
cert.pem
required UCRT64 dependency DLLs
applicable dependency licenses/notices
```

Do not disable TLS verification. A certificate error should be investigated by
checking the system clock, the configured `ca_bundle`, and the bundled
`cert.pem`.

## Audio

Windows playback currently uses the packaged libao live-output path, which
selects its Windows multimedia (WMM) driver. FFmpeg decode, the final packed
signed-16-bit PCM path, queue transitions, pause/next, and spectrum observation
are shared with the Unix build.

During bring-up, captured WAV/M4A output played cleanly on macOS and physical
Windows hardware while one Parallels Desktop guest produced audible
popping/zapping. Treat that as a virtualization caveat, not evidence of corrupt
Signalbox PCM. Parallels guest audio is not the release quality reference; the
RC checklist still requires direct playback validation on physical Windows.

If Signalbox reports that no libao driver or audio device is available, verify
that the UCRT64 libao DLL/plugins are in the runtime bundle, Windows has a
working default output device, and no other application has made it
unavailable.

## Configuration and credentials

Windows configuration lives under `%APPDATA%\Signalbox` using native Known
Folder lookup and UTF-16 filesystem conversion at the platform boundary. The
config itself remains UTF-8.

Windows Credential Manager is not implemented yet. The TUI can accept a masked
login for the current session, but cannot securely remember it. A plaintext
`password` remains compatible but is not recommended. `password_command`,
event commands, audio pipes, and FIFO control are unavailable on Windows; the
program reports those limitations instead of silently pretending they work.

## Album-art fallback

Album art uses ANSI half blocks through the shared renderer rather than a
terminal-specific image protocol. It uses truecolor or the 256-color fallback
when detected and hides art cleanly when color capability or layout space is
insufficient. The text metadata, lyrics, and playback UI remain available when
art is hidden or unavailable.

## Known limitations

- Live horizontal dragging in Windows Terminal may show temporary tearing with
  either PDCursesMod backend; the final redraw after resize is correct.
- Credential Manager, Named Pipe control, Windows packaging, and Windows CI are
  not complete.
- The current source history records native Windows 11 build/TUI validation and
  clean captured-audio playback on physical Windows. A complete physical-host
  RC matrix covering fresh launch, real-time playback, TUI resize, station
  switching, art, lyrics, TLS, and relaunch/cache reuse is still pending.
- Release bundles must be checked on a clean machine with no MSYS2 installation
  and must not depend on `msys-2.0.dll`.

## Optional diagnostics

`SIGNALBOX_DEBUG_TUI=1` writes support diagnostics to
`signalbox-tui-debug.log`; it does not write them over the curses screen.
`SIGNALBOX_DEBUG_KEYS=1` additionally creates `signalbox-keys.log` on the
PDCursesMod build. Password input is redacted. Diagnostic logs can contain
track metadata, station-filter text, provider identifiers, paths, and URLs, so
review them before sharing.

These diagnostics are for focused support sessions only. The retired PCM and
decoder-capture environment variables from Windows audio bring-up are no
longer part of the implementation.
