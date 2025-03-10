#! /bin/sh

case "$1" in
    start)
    	echo "Starting simpleserver"
    	start-stop-daemon -S -n simpleserver /usr/bin/aesdsocket -- -d
    	;;
    stop)
    	echo "Stopping simpleserver"
    	start-stop-daemon -k -n simpleserver
    	;;
    *)
    	echo "Usage: $0 {start|stop}"
    	exit 1
esac

exit 0
