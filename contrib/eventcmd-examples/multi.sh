#!/bin/bash
#
# Usage
# =====
# 
# Set
# 
# 	event_command = /path/to/multi.sh
# 
# in Signalbox's config file. Then create an eventcmd.d directory alongside
# the active config file, move your eventcmd scripts there, and make
# them executable (chmod +x). They will be run in an unspecified order the same
# way they would have been run if Signalbox called them directly (i.e. using
# event_command).

CONFIG_HOME=${XDG_CONFIG_HOME:-"$HOME/.config"}
if [[ -f "$CONFIG_HOME/signalbox/config" || ! -f "$CONFIG_HOME/pianobar/config" ]]; then
	EVENTCMD_DIR="$CONFIG_HOME/signalbox/eventcmd.d"
else
	EVENTCMD_DIR="$CONFIG_HOME/pianobar/eventcmd.d"
fi

STDIN=$(mktemp "${TMPDIR:-/tmp}/signalbox.XXXXXX")
cat >> "$STDIN"

for F in "$EVENTCMD_DIR"/*; do
	if [ -x "$F" ]; then
		"$F" "$@" < "$STDIN"
	fi
done

rm "$STDIN"
