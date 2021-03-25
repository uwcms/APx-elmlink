#!/bin/sh

start ()
{
	echo " starting elmlink"
	mkdir -p /var/run/elmlink
	elmlink /opt/elmlink/elmlink.conf &
}
stop ()
{
	killall	elmlink
	echo " No stop action"
}
restart()
{
	stop
	start
}

case "$1" in
	start)
		start; ;;
	stop)
		stop; ;;
	restart)
		restart; ;;
	*)
		echo "Usage: $0 {start|stop|restart}"
		exit 1
esac

exit $?
