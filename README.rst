Signalbox
=========

**A modern open-source terminal client for Pandora.**

Signalbox is an early-stage continuation and modernization of `pianobar`_, the
console Pandora client created by Lars-Dominik Braun. It begins with pianobar's
working implementation and preserved history, then aims to build a maintainable
terminal application around that foundation.

Signalbox is not yet a distinct end-user release. The current program builds
and runs as ``signalbox`` while much of its inherited behavior and internal
naming remains unchanged.

.. _pianobar: https://github.com/PromyLOPh/pianobar

Project status
--------------

Development is at **Phase 1: identity and build cleanup**. The baseline is
recorded by the tag ``upstream-baseline-2026-09-01`` and was verified on:

- macOS 26.6.2 on Apple Silicon (arm64)
- Homebrew 6.0.21
- Apple Clang 21.0.0
- FFmpeg 9.0.1

That baseline compiled, launched, authenticated with Pandora, retrieved the
account's stations, and played audio. See `docs/UPSTREAM.md`_ for provenance and
validation details and `docs/ROADMAP.md`_ for planned work.

.. _docs/UPSTREAM.md: docs/UPSTREAM.md
.. _docs/ROADMAP.md: docs/ROADMAP.md

Current capabilities
--------------------

The current code inherits pianobar's console interface and supports:

- Pandora authentication and station playback
- station selection, creation, renaming, deletion, and seed management
- love, ban, tired, bookmark, and skip actions
- song history and upcoming-track display
- pause/resume and software volume control
- configurable key bindings and output formats
- proxy, FIFO remote-control, audio-pipe, and event-command interfaces

These are inherited capabilities, not a claim that Signalbox's planned
interface is complete.

Direction
---------

Signalbox is intended to add:

- a modern interactive TUI with station and now-playing views
- playback progress, history, keyboard navigation, resize handling, and themes
- macOS Now Playing, media keys, notifications, and Keychain integration
- Linux MPRIS, desktop notifications, and secret-service integration
- robust reconnect and error handling
- a deliberate CLI/headless mode alongside the TUI
- optional terminal album artwork where supported
- Homebrew and Linux packaging, CI builds, and release artifacts

macOS and Linux are the target platforms. Modern macOS on Apple Silicon has
been manually validated through playback, and CI continuously validates that
Signalbox builds on current GitHub-hosted macOS and Ubuntu runners. Linux
runtime playback has not yet been validated by the Signalbox project.

Build from source on macOS
--------------------------

The verified build uses Homebrew packages and GNU Make. Install the current
dependencies:

.. code-block:: console

   brew install ffmpeg json-c libao libgcrypt ncurses pkgconf

Then build from the repository root:

.. code-block:: console

   gmake clean && gmake

The build also requires pthreads, libcurl, and the wide-character ncurses
library (``ncursesw``). pthreads and libcurl were available in the
verified macOS environment; ``pkgconf`` must be able to locate all dependencies.
No global installation is required.

Continuous integration
----------------------

GitHub Actions builds Signalbox on ``ubuntu-latest`` and ``macos-latest`` for
pushes and pull requests involving the ``main`` or ``develop`` branches. CI
checks contrib script syntax, verifies that ``./signalbox`` is executable, and
prints platform-appropriate binary linkage diagnostics.

These checks only validate compilation and lightweight, non-authenticated
artifacts. They do not use Pandora credentials and do not test authentication,
station retrieval, network playback, audio output, or runtime configuration
selection. The macOS playback result described above was verified manually and
is separate from CI; Linux playback remains unverified.

Usage
-----

Start the executable from the source tree with:

.. code-block:: console

   ./signalbox

Classic mode remains the default. To launch the experimental Phase C4
full-screen interface, use an interactive terminal and run:

.. code-block:: console

   ./signalbox --tui

Choose a palette with ``--theme phosphor`` (the default), ``amber``, ``mono``,
or ``neutral``. Setting ``NO_COLOR`` forces the monochrome presentation.

The TUI requires stdin and stdout to be terminals and a usable ``TERM`` value;
it exits with a concise error instead of initializing curses otherwise. The
shell opens immediately; credential prompts temporarily restore classic mode.
The shell shows the real station list, distinct selected and active stations,
artist/title/album/station metadata, rating, signed-dB software volume,
playback state and adaptive progress, transient status notices, and up to ten
recent tracks from the current session. History is shown only when the terminal
has room. Navigate with arrows or ``j``/``k``, press Enter to tune, and use the
configured keys for create, rename, delete, pause/resume, next, love, ban,
volume down/up/reset, help, and quit. Native management includes create/search,
add-music search, QuickMix membership, genre browsing, shared-station IDs,
create-from-current-song, song/artist bookmarks, session-history actions, and
station seed/feedback/mode lists. Destructive operations use explicit
confirmations. The inherited station-select key
shows browser guidance instead of opening a blocking prompt. Other modal
actions involving account settings and startup credentials remain classic-only;
use classic mode for the complete inherited interface. The upcoming modal
browses the already-fetched queue and deliberately adds no queue mutations.

On first run, enter Pandora credentials when prompted. TUI mode starts with the
autostart station, or the first available station when none is configured.
Press ``?`` for the current key bindings and ``q`` to quit. Configuration is
read from ``$XDG_CONFIG_HOME/signalbox/config`` (normally
``~/.config/signalbox/config``). If that file does not exist, Signalbox falls
back to ``$XDG_CONFIG_HOME/pianobar/config`` (normally
``~/.config/pianobar/config``), so existing pianobar users do not need to move
their configuration immediately. If both files exist, the Signalbox file wins;
the files are never merged or copied automatically. The annotated
`configuration example`_ documents available settings.

.. _configuration example: contrib/config-example

Contrib helpers
---------------

User-facing helper scripts and event-command examples are available in
``contrib/``. The headless wrapper is ``contrib/headless_signalbox``; the old
``contrib/headless_pianobar`` name remains as a lightweight compatibility entry
point. FIFO helpers prefer the Signalbox configuration directory and follow the
application's fallback to a legacy pianobar configuration when it is active.

Development expectations
------------------------

The repository is intentionally close to upstream today. Changes should remain
small and reviewable, preserve attribution, and avoid mixing identity work with
protocol or playback changes. Compatibility claims should be backed by a
recorded build or runtime test. The current and proposed component boundaries
are described in `docs/ARCHITECTURE.md`_.

.. _docs/ARCHITECTURE.md: docs/ARCHITECTURE.md

Contributing
------------

Signalbox is at the stage where focused bug reports, platform build results,
documentation corrections, and narrowly scoped patches are most useful. Before
starting a substantial feature, open a discussion in the repository so its
scope and architectural fit can be agreed. Include the platform and dependency
versions used to test code changes, and keep unrelated cleanup in separate
commits.

License and attribution
-----------------------

Signalbox's inherited pianobar code is distributed under the **MIT License**, as
stated in ``COPYING`` and the source-file headers. Copyright notices for
Lars-Dominik Braun and other upstream contributors remain part of the source and
must be preserved. The full upstream history is retained as an additional record
of authorship and provenance.

Pandora is a third-party service and trademark. Signalbox is an independent
open-source project and is not affiliated with or endorsed by Pandora.
