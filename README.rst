Signalbox
=========

|CI| |License| |Platforms|

**Pandora radio, tuned for the terminal.**

Signalbox is a modern, open-source terminal client for Pandora: a responsive
phosphor TUI, real-time audio spectrum, fast keyboard navigation, and the full
station-management foundation inherited from pianobar.

.. image:: docs/assets/signalbox-tui.svg
   :alt: Signalbox phosphor terminal interface showing stations, now playing,
         upcoming tracks, and listening history
   :align: center

.. image:: docs/assets/signalbox-spectrum.svg
   :alt: Signalbox real-time spectrum analyzer in the phosphor terminal theme
   :align: center

.. |CI| image:: https://github.com/simplycole/signalbox/actions/workflows/build.yml/badge.svg?branch=main
   :target: https://github.com/simplycole/signalbox/actions/workflows/build.yml
   :alt: Build status
.. |License| image:: https://img.shields.io/badge/license-MIT-39ff88.svg
   :target: COPYING
   :alt: MIT License
.. |Platforms| image:: https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-58d6ff.svg
   :alt: macOS, Linux, and Windows

Why Signalbox?
--------------

Signalbox keeps pianobar's lean native-C core and proven Pandora integration,
then builds a more discoverable terminal experience around it. It is currently
an early-stage project: the TUI is the default on supported interactive
terminals, while classic pianobar-compatible mode remains available for
compatibility and headless use.

Downloads
---------

The latest release is `Signalbox 0.1.1`_. Matching ``.sha256`` checksum files
are available on the release page.

macOS
~~~~~

- **Apple Silicon / arm64**
- Download `signalbox-0.1.1-macos-arm64.tar.gz`_.
- Install the required Homebrew runtime dependencies with
  ``brew install ffmpeg libgcrypt json-c libao``.
- See the included ``README.txt`` for package-specific setup and usage.

Linux
~~~~~

- **x86_64**
- Download `signalbox-0.1.1-linux-x86_64.tar.gz`_.
- This package is dynamically linked for the tested Linux environment.
- See the included ``README.txt`` for dependencies, setup, and usage.

Windows
~~~~~~~

Native Windows support is implemented and has been exercised on Windows 11,
but v0.1.1 does not include a downloadable Windows binary. Portable Windows
packaging remains future work.

Source
~~~~~~

GitHub automatically provides source-code ZIP and tar.gz archives on the
release page. Building from source remains fully supported.

.. _Signalbox 0.1.1: https://github.com/simplycole/signalbox/releases/tag/v0.1.1
.. _signalbox-0.1.1-macos-arm64.tar.gz: https://github.com/simplycole/signalbox/releases/download/v0.1.1/signalbox-0.1.1-macos-arm64.tar.gz
.. _signalbox-0.1.1-linux-x86_64.tar.gz: https://github.com/simplycole/signalbox/releases/download/v0.1.1/signalbox-0.1.1-linux-x86_64.tar.gz

Highlights
----------

- Responsive ``ncursesw`` interface with phosphor, amber, neutral, and
  monochrome themes
- Real PCM-driven 8/12-band spectrum analyzer with smoothing and peak hold
- Terminal-native cached album art using ANSI half blocks with
  truecolor/256-color fallback
- Searchable retained station pane with instant name filtering and safe
  switching through the existing playback pipeline while Now Playing remains
  visible; views sort A-Z or retain original Pandora order
- Now-playing metadata, adaptive progress, signed-dB volume, upcoming queue,
  and full in-memory session history
- Synced LRCLIB lyrics with an adaptive previous/current/next strip and
  current-line highlighting in the full Lyrics view
- Background MusicBrainz enrichment, Cover Art Archive resolution, queue
  prefetch, and schema-versioned persistent enrichment and artwork caches
- Native TUI flows for station creation, rename/delete, QuickMix, genres,
  seeds, feedback, bookmarks, and station modes
- Configurable bindings with an in-app, responsive, scrollable HELP overlay
- Secure saved credentials through macOS Keychain or Linux Secret Service;
  plaintext is never written by the TUI
- Classic UI, FIFO remote control, audio pipe, proxy, and event-command
  compatibility inherited from pianobar

Controls
--------

Press ``?`` in the TUI for the authoritative list: configured action bindings
are reflected there automatically.

.. list-table:: Default TUI controls
   :header-rows: 1
   :widths: 28 72

   * - Key
     - Action
   * - ``↑``/``↓``, ``j``/``k``
     - Move through the focused list
   * - ``PgUp``/``PgDn``
     - Move by a page
   * - ``Home``/``End``
     - Jump to first/last item
   * - ``s``
     - Focus the existing Stations pane
   * - ``Enter``
     - Switch the selected station or open focused actions
   * - ``Tab``/``Shift+Tab``
     - Switch between Stations and Recent
   * - ``/``
     - Edit the focused station pane's case-insensitive name filter
   * - ``z``
     - Cycle the station view between A-Z and original Pandora order
   * - ``v``
     - Create a station from the current song or artist
   * - ``p`` / ``n``
     - Pause or resume / next track
   * - ``+`` / ``-``
     - Love / ban
   * - ``(`` / ``)`` / ``^``
     - Volume down / up / reset to 0 dB
   * - ``h`` / ``u``
     - Session history / upcoming tracks
   * - ``V``
     - Toggle the visualizer
   * - ``i`` / ``L``
     - Open Track Info / Lyrics; arrows or ``j``/``k`` scroll, Esc closes
   * - ``?`` / ``q``
     - HELP / quit

The inherited action keys can be remapped in the config. Lowercase ``v`` opens
Create Station From for the current song or artist. Uppercase ``V`` is
available for the visualizer only when it does not conflict with a configured
action.

Set ``lyrics_display = three-line`` (the default), ``line``, or ``off`` in
the config to control inline synced lyrics. The full Lyrics view remains
available when the inline display is off or only plain lyrics are available.

Build and run
-------------

Use one of the packaged releases above, or build Signalbox from source using
the platform instructions below.

Build from source
~~~~~~~~~~~~~~~~~

Signalbox targets macOS, Linux, and native Windows x64. On Unix you need a C99 compiler,
``pkg-config``, FFmpeg (``libavcodec``, ``libavformat``, ``libavutil``, and
``libavfilter`` plus ``libswscale``), libcurl, libgcrypt, json-c, libao,
pthreads, and ``ncursesw``.

macOS (Homebrew)
~~~~~~~~~~~~~~~~

.. code-block:: console

   brew install ffmpeg json-c libao libgcrypt ncurses pkgconf make
   gmake

Debian / Ubuntu
~~~~~~~~~~~~~~~

.. code-block:: console

   sudo apt-get install build-essential libao-dev libavcodec-dev \
     libavfilter-dev libavformat-dev libavutil-dev libswscale-dev libcurl4-gnutls-dev \
     libgcrypt20-dev libjson-c-dev libncursesw5-dev libsecret-1-dev pkg-config
   make

Launch Signalbox (the full-screen TUI is selected automatically in a supported
interactive terminal):

.. code-block:: console

   ./signalbox

Useful options:

.. code-block:: console

   ./signalbox --theme phosphor
   ./signalbox --visualizer off
   ./signalbox --tui
   ./signalbox --classic
   ./signalbox --forget-credentials
   ./signalbox --version

Install under ``/usr/local`` with ``sudo make install`` (or ``gmake install``
on macOS). Override ``PREFIX`` or use ``DESTDIR`` for packaging. Windows uses
MSYS2 UCRT64, PDCursesMod WinCon, and the libao/WMM audio backend; see
`Windows build and runtime notes`_. See the `annotated configuration`_ for
settings and key remapping.

.. _annotated configuration: contrib/config-example
.. _Windows build and runtime notes: docs/WINDOWS.md

Configuration and credentials
-----------------------------

Signalbox reads ``$XDG_CONFIG_HOME/signalbox/config`` (normally
``~/.config/signalbox/config``). If it is absent, the legacy
``$XDG_CONFIG_HOME/pianobar/config`` is used as a compatibility fallback; files
are never merged or migrated automatically.

In TUI mode, passwords can be stored in macOS Keychain or a Linux Secret
Service provider such as GNOME Keyring/KWallet. Linux support is compiled when
``libsecret-1`` is available and fails closed when the service cannot be
reached. Only the selected account email is written to Signalbox's
mode-``0600`` account file. Explicit ``password`` and ``password_command``
settings remain supported for compatibility.

pianobar lineage
----------------

Signalbox is a continuation of `pianobar`_, created by Lars-Dominik Braun. Its
working Pandora protocol/player implementation, MIT license, attribution, and
complete Git history are intentionally preserved. The ``upstream`` remote
tracks the canonical project, and ``upstream-baseline-2026-09-01`` records the
verified starting point for Signalbox development.

Signalbox is independent and is not affiliated with or endorsed by Pandora.
Pandora is a third-party service and trademark.

.. _pianobar: https://github.com/PromyLOPh/pianobar

Roadmap
-------

Near-term work is deliberately release-focused:

1. Linux Secret Service runtime validation across selected desktops
2. Complete physical-Windows RC validation, Credential Manager integration,
   and portable Windows packaging
3. Continue classic/FIFO/headless compatibility validation with the TUI as the
   interactive default
4. Persistent listening history with explicit retention and privacy behavior
5. Richer visualizer modes and evaluation of optional terminal-image protocols
6. Homebrew and native Linux package-manager integration

Native Windows x64 builds use the shared TUI renderer with PDCursesMod WinCon,
native Win32 input, FFmpeg, and libao/WMM. Build/TUI/audio work has been exercised
on Windows 11, including clean audio capture playback on physical Windows; the
complete release-candidate matrix, Credential Manager integration, packaging,
and CI remain pending. See the detailed `roadmap`_, `TUI design`_, `architecture`_,
`upstream record`_, and `QA checklist`_.

.. _roadmap: docs/ROADMAP.md
.. _TUI design: docs/TUI.md
.. _architecture: docs/ARCHITECTURE.md
.. _upstream record: docs/UPSTREAM.md
.. _QA checklist: docs/QA.md

Contributing
------------

Focused bug reports, platform build results, documentation corrections, and
narrow patches are welcome. Include platform and dependency versions with test
results, preserve upstream attribution, and keep unrelated behavior changes in
separate commits.

License
-------

Signalbox and its inherited pianobar sources are distributed under the
`MIT License`_. Copyright notices for Lars-Dominik Braun and other contributors
remain in the source and history.

.. _MIT License: COPYING
