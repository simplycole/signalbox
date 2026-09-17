Signalbox @VERSION@ — macOS Apple Silicon (arm64)
==================================================

This package targets Apple Silicon Macs and was tested on the current macOS
environment used by the Signalbox project. It is not an Intel macOS package.

Signalbox uses Homebrew-provided shared libraries. Install them before running:

    brew install ffmpeg libgcrypt json-c libao

Run Signalbox from this directory:

    chmod +x signalbox
    ./signalbox --version
    ./signalbox --tui

To install the executable for all users:

    sudo install -m 0755 signalbox /usr/local/bin/signalbox

Signalbox reads $XDG_CONFIG_HOME/signalbox/config, normally
~/.config/signalbox/config. The included config-example documents available
settings. For example:

    mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/signalbox"
    cp config-example "${XDG_CONFIG_HOME:-$HOME/.config}/signalbox/config"

COPYING contains the Signalbox license. THIRD_PARTY_NOTICES.txt describes the
external shared libraries expected at runtime; those libraries are not bundled.
