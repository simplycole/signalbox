#!/bin/sh

# Extract a shared station id from its homepage and add it to Signalbox.

config_home=${XDG_CONFIG_HOME:-"$HOME/.config"}
if [ -f "$config_home/signalbox/config" ] || [ ! -f "$config_home/pianobar/config" ]; then
	ctl="$config_home/signalbox/ctl"
else
	ctl="$config_home/pianobar/ctl"
fi

if [ -z "$1" ]; then
	echo "Usage: `basename $0` <station url>"
	exit 1
fi

curl -s "$1" | sed -nre "s#.*launchStationFromId\('([0-9]+)'\).*#j\1#gp" > "$ctl"

exit 0
