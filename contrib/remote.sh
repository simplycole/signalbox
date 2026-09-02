#!/bin/sh

echocmd="/bin/echo -n"
config_home=${XDG_CONFIG_HOME:-"$HOME/.config"}
if [ -f "$config_home/signalbox/config" ] || [ ! -f "$config_home/pianobar/config" ]; then
	ctlfile="$config_home/signalbox/ctl"
else
	ctlfile="$config_home/pianobar/ctl"
fi

# signalbox running? echo would block otherwise
ps -C 'signalbox' > /dev/null

if [ $? -ne 0 ]; then
	echo 'naughty.notify({title = "signalbox", text = "Not running"})' | awesome-client -
	exit 1;
fi

case "$1" in
	pp)
		$echocmd p > "$ctlfile"
		;;
	next)
		$echocmd n > "$ctlfile"
		;;
	love)
		$echocmd + > "$ctlfile"
		;;
	ban)
		$echocmd - > "$ctlfile"
		;;
esac
