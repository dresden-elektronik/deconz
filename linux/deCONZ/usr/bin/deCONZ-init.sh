#!/bin/bash
MAINUSER=$(getent passwd 1000 | cut -d: -f1)
DECONZ_CONF_DIR="/home/$MAINUSER/.local/share"
OS_ID=$(cat /etc/os-release | sed -n 's/^ID=\(.*\)/\1/p')
PHOSCON_SDCARD="02544d534130384722"
THIS_SDCARD=$(cat /sys/block/mmcblk0/device/cid)
THIS_SDCARD=$(echo ${THIS_SDCARD:0:18})

LOG_EMERG="<0>"
LOG_ALERT="<1>"
LOG_CRIT="<2>"
LOG_ERROR="<3>"
LOG_WARN="<4>"
LOG_NOTICE="<5>"
LOG_INFO="<6>"
LOG_DEBUG="<7>"

ENABLE_SERVICE=0
DOCKER=0

cat /proc/1/cgroup | grep docker > /dev/null

if [ $? -eq 0 ]; then
	DOCKER=1
fi

if [ $DOCKER -eq 0 ] && [ "$OS_ID" = "raspbian" ]; then
	ENABLE_SERVICE=1
fi

# managed defines if deCONZ should manage the system, which is true for de sd card images
#MANAGED=`find /home/pi/.local -name 'gw-version'`

#if [ ! -z "$MANAGED" ]; then
#	GW_VERSION=$(cat $MANAGED)
#	echo "$GW_VERSION"

	#if [[ $THIS_SDCARD == $PHOSCON_SDCARD ]]; then
		# TODO enable service if specific version
	#fi
#fi

drs=("data/dresden-elektronik/deCONZ/zll.db" "dresden-elektronik/deCONZ/zll.db" "deCONZ/zll.db")
for i in "${drs[@]}"; do
	if [ -f "${DECONZ_CONF_DIR}/$i" ]; then
		ZLLDB="${DECONZ_CONF_DIR}/$i"
		break
	fi
done

if [ ! -f "$ZLLDB" ]; then
	# might have been deleted
	ZLLDB=""
fi

if [[ -n "$ZLLDB" ]]; then
	# correct file ownership if needed
	[[ $(stat -c %U $ZLLDB) != $MAINUSER ]] && chown $MAINUSER $ZLLDB
fi

## check wifi service ##
# enable WiFi service just for Phoscon gateways which
# have a printet label with WiFi password

systemctl is-active --quiet deconz-wifi
if [ $? -ne 0 ]; then
	if [ $ENABLE_SERVICE -eq 1 ]; then
		echo "${LOG_INFO} starting deconz-wifi.service"
		systemctl start deconz-wifi
		systemctl enable deconz-wifi
	fi
fi

if [ $ENABLE_SERVICE -eq 0 ]; then
	echo "${LOG_INFO} stopping deconz-wifi.service"
	systemctl stop deconz-wifi
	systemctl disable deconz-wifi
fi
