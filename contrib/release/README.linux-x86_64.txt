Signalbox @VERSION@ — Linux x86_64
=================================

This dynamically linked package targets x86_64 Linux and is built and tested
on the Ubuntu environment used by Signalbox GitHub Actions. It is not a
universal Linux binary.

The executable requires FFmpeg libraries (including libswscale), libcurl,
libgcrypt, json-c, libao, ncursesw, pthreads, and libsecret. On Debian/Ubuntu,
the matching development packages used by CI can be installed with:

    sudo apt-get install libao-dev libavcodec-dev libavfilter-dev \
      libavformat-dev libavutil-dev libswscale-dev libcurl4-gnutls-dev \
      libgcrypt20-dev libjson-c-dev libncursesw5-dev libsecret-1-dev

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
