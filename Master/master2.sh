while true
do
/usr/local/AccesSystem/Master/master.py \
	-c /usr/local/etc/master/acnode.ini \
	--dbfile /usr/local/etc/master/keydb.txt \
	-v \
	--leeway 2462023202 2>&1 | tee /tmp/master.err.log

cat /tmp/master.err.log | mail -s "Spurious master crash." dirkx@webweaving.org
done
