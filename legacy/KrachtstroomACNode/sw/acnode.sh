#!/bin/sh
set -e 
cd /home/pi/AccesSystem/KrachtstroomACNode/sw
/usr/bin/screen -S ACNode -d -m  /usr/bin/python2.7 /home/pi/AccesSystem/KrachtstroomACNode/sw/acnode-client.py -vvv
exit $?

