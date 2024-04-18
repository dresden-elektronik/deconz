#!/bin/bash
OS_ID=$(cat /etc/os-release | sed -n 's/^ID=\(.*\)/\1/p')
OS_VERSION=$(cat /etc/os-release | sed -n 's/^VERSION="[0-9]\s(\(.*\))"/\1/p')

if [ "$OS_ID" == "ubuntu" ]; then
	# check dependencies
	deps=("libqt5core5a" "libqt5network5" "libqt5widgets5" "libqt5gui5" "libqt5serialport5" "libqt5websockets5" "libqt5sql5" "libcap2-bin" "sqlite3")
	for i in "${deps[@]}"; do
		dpkg -l $i 2> /dev/null | grep '^ii' > /dev/null

		if [ $? -ne 0 ]; then
			apt-get -y install $i
			if [ $? -ne 0 ]; then
				exit 3
			fi

		fi
	done

	# dialout group for ConBee access, user is arg1
	groups $1 | grep &>/dev/null 'dialout'
	if [ $? -ne 0 ]; then
		gpasswd -a $1 "dialout"
		if [ $? -ne 0 ]; then
			exit 2
		fi
	fi

	exit 0
fi

