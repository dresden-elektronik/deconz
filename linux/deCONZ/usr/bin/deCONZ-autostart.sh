#!/bin/bash
APPNAME=deCONZ

# prevent SIGILL signal in openssl library
EXTRA_OPTIONS=${DECONZ_OPTS:-}

UPDATE_FW_SCRIPT="/var/tmp/deCONZ-update-firmware.sh"

#BASEURL="http://www.dresden-elektronik.de/rpi"

OTAUDIR="/home/${USER}/otau"
RUNDIR="$XDG_RUNTIME_DIR/deconz"
PIDFILE="${RUNDIR}/deconz.pid"

# set date behind a proxy (no NTP/UDP)
# sudo date -s "$(curl --head -i - duckduckgo.com | grep '^Date:' | cut -d' ' -f3-6)Z"

#check if deCONZ already running
pidof deCONZ
if [ $? -eq "0" ]; then
	echo "deCONZ already running"
	exit 1
fi

# auto configure system
OS_ID=$(cat /etc/os-release | sed -n 's/^ID=\(.*\)/\1/p')
OS_VERSION=$(cat /etc/os-release | sed -n 's/^VERSION="[0-9]\s(\(.*\))"/\1/p')

if [ "$OS_ID" = "raspbian" ]; then
	echo "start deCONZ via systemd service"
	sudo systemctl start deconz-gui
	exit 0
fi

if [ "$OS_ID" = "ubuntu" ]; then
	# check dependencies
	DOSETUP=0
	NOGROUP=0

	deps=("libqt5core5a" "libqt5network5" "libqt5widgets5" "libqt5gui5" "libqt5serialport5" "libqt5websockets5" "libqt5sql5" "libcap2-bin" "sqlite3")
	for i in "${deps[@]}"; do
		dpkg -l $i 2> /dev/null | grep '^ii' > /dev/null

		if [ $? -ne 0 ]; then
			DOSETUP=1
		fi
	done

	groups | grep &>/dev/null 'dialout'
	if [ $? -ne 0 ]; then
		NOGROUP=1
	fi

	if [ $DOSETUP -eq 1 ] || [ $NOGROUP -eq 1 ]; then

		zenity --title="deCONZ Setup" --question --text="Some missing dependencies must be install as super user.\nProceed?"

		if [ $? -eq 0 ]; then

			pkexec /usr/bin/deCONZ-setup.sh $USER

			case $? in
			0)
				# OK
				;;
			3)
				zenity --title="deCONZ Setup" --error --text="Failed to install dependencies\n ${deps[@]}"
				exit 1
				;;
			*)
				zenity --title="deCONZ Setup" --error --text="An error occured during setup $?"
				exit 1
				;;
			esac

			if [ $NOGROUP -eq 1 ]; then
				# need relogin
#				zenity --title="deCONZ Setup" --info --text="Modified system configuration to provide to access the USB dongle.\nPlease logout and login again to activate the changes."
				# replace process with new one which is part of the group (whole script will be executed again)
				exec sg dialout deCONZ-autostart.sh
				#exit 1
			fi
		else
			exit 1
		fi
	fi
fi

# create otau directory if not exist
if [ ! -d "$OTAUDIR" ]; then
    mkdir -p ${OTAUDIR}
fi

while [ 1 ]
do
	# provide service on port 80
	which setcap
	hassetcap=$?
	OPTIONS="--auto-connect=1 --dbg-error=1"

	if [ $hassetcap -eq 0 ] && [ "$OS_ID" == "raspbian" ]; then

		## can use privileged ports?
		setcap -v -q cap_net_bind_service,cap_sys_time=+ep /usr/bin/deCONZ
		if [ $? -eq 0 ]; then
			OPTIONS+=" --http-port=80"
		else
			OPTIONS+=" --http-port=8080"
		fi
	else
		OPTIONS+=" --http-port=8080"
	fi

	OPTIONS+=" --pid-file=$PIDFILE"

	# print backtrace on SEGV
	#export LD_PRELOAD=/usr/lib/debug/lib/arm-linux-gnueabihf/libSegFault.so

	$APPNAME $OPTIONS $EXTRA_OPTIONS
	RC=$?
	echo "deCONZ - exit code ${RC}"

	case "$RC" in
	0) # Normal exit
		rm -f $PIDFILE
		exit 0
		;;
	40) # Download and install updates
		rm -f $PIDFILE
		deCONZ-update.sh
		;;
	41) # Restart this script in a new process
		rm -f $PIDFILE
		exec $@
		;;
	42) # Download and install updates (beta channel)
		rm -f $PIDFILE
		deCONZ-update.sh -c beta
		;;
	43) # Restart system
		rm -f $PIDFILE
		sudo reboot
		exit 0
		;;
	44) # Shutdown system
		rm -f $PIDFILE
		sudo halt
		exit 0
		;;
	45) # Download and install updates (alpha channel)
		# not yet supported just do a beta channel update
		# deCONZ-update.sh -c alpha
		rm -f $PIDFILE
		deCONZ-update.sh -c beta
		;;
	46) # Update firmware only
		if [ -e "$UPDATE_FW_SCRIPT" ]; then
			# todo will be replaced by systemd deconz-update service
			sh $UPDATE_FW_SCRIPT
			# cleanup
			echo "delete $UPDATE_FW_SCRIPT"
			rm $UPDATE_FW_SCRIPT
		else
			echo "$UPDATE_FW_SCRIPT not found"
		fi
		;;
	126) # Permission problem or command is not executable
		rm -f $PIDFILE
		finish
		exit 1
		;;
	127) # Command not found
		finish
		exit 1
		;;
	130) # CTRL-C, SIGINT (128 + 2)
		finish
		exit 0
		;;
	137) # SIGKILL (128 + 9)
		finish
		exit 0
		;;
	139) # SEGV (128 + 11)
		rm -f $PIDFILE
		echo "SEGV received, wait 10s and restart deCONZ"
		sleep 10
		;;
	143) # SIGTERM (128 + 15)
		rm -f $PIDFILE
		finish
		exit 0
		;;
	*) echo "exit code $RC not handled"
		;;
	esac
done
