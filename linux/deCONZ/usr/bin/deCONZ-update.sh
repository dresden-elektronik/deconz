#!/bin/bash

# Update channels:
#     latest (stable)
#     beta
UPDATE_CHANNEL="latest"

# check if deCONZ is running and quit if so
if [ "$(pidof deCONZ)" ]; then
	echo "deCONZ is running, please close before proceed with update"
	exit 1
fi

# handle command line arguments
while getopts ":c:" opt; do
  case $opt in
    c) # update channels
      UPDATE_CHANNEL=$OPTARG
      ;;
  esac
done

USCRIPT="rpi-deconz-${UPDATE_CHANNEL}.sh"
UURL="http://www.dresden-elektronik.de/rpi/deconz/${USCRIPT}"

cd /tmp

# remove old update script if exist
if [ -e "$USCRIPT" ]; then
	rm $USCRIPT

	if [ $? -ne 0 ]; then
		exit 1
	fi
fi

# get latest update script
wget $UURL

if [ $? -ne 0 ]; then
	exit 1
fi

# Download ok now install
chmod +x $USCRIPT
exec ./$USCRIPT
