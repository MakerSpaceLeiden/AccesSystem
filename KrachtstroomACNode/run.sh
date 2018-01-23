#!/bin/sh
while true
do
# python /home/pi/AccesSystem/KrachtstroomACNode/hw/on.py
/home/pi/AccesSystem/KrachtstroomACNode/acnode-client.py --config /etc/acnode.ini
sleep 5
done


