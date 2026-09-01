Signalbox
=========

**A modern open-source terminal client for Pandora.**

Signalbox is an early-stage continuation and modernization of `pianobar`_, the
console Pandora client created by Lars-Dominik Braun. It begins with pianobar's
working implementation and preserved history, then aims to build a maintainable
terminal application around that foundation.

Signalbox is not yet a distinct end-user release. The current program builds
and runs as ``signalbox`` while its inherited behavior, configuration paths,
and internal names remain unchanged.

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

macOS and Linux are the target platforms. Modern macOS on Apple Silicon is the
only environment validated by the Signalbox project so far. Linux remains a
first-class goal and pianobar has an established history there, but Signalbox
has not yet recorded its own Linux validation matrix.

Build from source on macOS
--------------------------

The verified build uses Homebrew packages and GNU Make. Install the current
dependencies:

.. code-block:: console

   brew install ffmpeg json-c libao libgcrypt pkgconf

Then build from the repository root:

.. code-block:: console

   gmake clean && gmake

The build also requires pthreads and libcurl. They were available in the
verified macOS environment; ``pkgconf`` must be able to locate all dependencies.
No global installation is required.

Usage
-----

Start the executable from the source tree with:

.. code-block:: console

   ./signalbox

On first run, enter Pandora credentials when prompted, then select a station.
Press ``?`` for the current key bindings and ``q`` to quit. Existing pianobar
configuration is read from ``$XDG_CONFIG_HOME/pianobar/config`` or
``~/.config/pianobar/config``. The annotated `configuration example`_ documents
available settings.

.. _configuration example: contrib/config-example

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
