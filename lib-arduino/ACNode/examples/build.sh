#!/bin/sh
set -e

if [ $# != 1 ]; then
	echo "Syntax: $0 <directory with .ino file>"
	exit 1
fi
DIR=$1
BASE=$(basename "$DIR")

if ! test -d "$DIR"; then
	echo "Directory $DIR not found."
	exit 2
fi

cd "$DIR";
if ! test -f "$BASE.ino"; then
	echo "$BASE.ino not found."
	exit 3
fi

exec arduino-cli compile --fqbn  esp32:esp32:esp32da  --build-property build.partitions=min_spiffs  --build-property upload.maximum_size=1966080  --warnings more

