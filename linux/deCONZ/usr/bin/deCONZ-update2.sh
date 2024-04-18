#!/bin/bash

# script-version: 3

STATE_READY_INSTALL="allreadytoinstall"
STATE_NO_UPDATE="noupdates"
STATE_INSTALLING="installing"
STATE_TRANSFERRING="transferring"
LAST_UPDATE_STATE=""
UPDATE_STATE=$STATE_NO_UPDATE
UPDATE_CHANNEL="latest"
USER1=$(getent passwd 1000 | cut -f1 -d:)
DECONZ_CONF_DIR="/home/${USER1}/.local/share"
DECONZ_PORT=0
ZLLDB=""
PROXY_ADDRESS=""
PROXY_PORT=""
LOG_EMERG="<0>"
LOG_ALERT="<1>"
LOG_CRIT="<2>"
LOG_ERROR="<3>"
LOG_WARN="<4>"
LOG_NOTICE="<5>"
LOG_INFO="<6>"
LOG_DEBUG="<7>"

DOCKER=0

cat /proc/1/cgroup | grep docker > /dev/null

if [ $? -eq 0 ]; then
	DOCKER=1
fi

function checkUpdate() {

	local pid=`pidof deCONZ`

	if [ -z "$pid" ]; then
		# deCONZ not running
		return
	fi

	# check local TCP ports in LISTEN state of deCONZ process
	# should return websocket and REST-API ports
	for port in `lsof -Panb -i4TCP -w -p $pid -Fn 2> /dev/null | grep "n\*" | cut -d: -f2 2> /dev/null`
	do
		# probe for REST-API port
		curl -s --noproxy 127.0.0.1 127.0.0.1:$port/api/config | grep bridgeid > /dev/null
		if [ $? -ne 0 ]; then
			continue
		fi

		if [[ "$DECONZ_PORT" != "$port" ]]; then
			DECONZ_PORT=$port
			echo "${LOG_DEBUG}found deCONZ port $DECONZ_PORT"
		fi
		break
	done

	if [ $DECONZ_PORT -eq 0 ]; then
		# can't access deCONZ REST-API
		return
	fi

	if [[ -z "$ZLLDB" ]]; then
		# look for latest config in specific order
		drs=("data/dresden-elektronik/deCONZ/zll.db" "dresden-elektronik/deCONZ/zll.db" "deCONZ/zll.db")
		for i in "${drs[@]}"; do
			if [ -f "${DECONZ_CONF_DIR}/$i" ]; then
				ZLLDB="${DECONZ_CONF_DIR}/$i"
				echo "${LOG_INFO}use database file $ZLLDB"
				break
			fi
		done

		# fallback when running as root; e.g. in a docker container
		if [ -z "$ZLLDB" ] && [ -d "/root/.local/share/dresden-elektronik" ]; then
			DECONZ_CONF_DIR="/root/.local/share"
			return
		fi
	fi

	if [ ! -f "$ZLLDB" ]; then
		# might have been deleted
		ZLLDB=""
	fi

	if [[ -z "$ZLLDB" ]]; then
		echo "${LOG_WARN}no database file (zll.db) found"
		return
	fi

	# is sqlite3 installed?
	sqlite3 --version &> /dev/null
	if [ $? -ne 0 ]; then
		echo "${LOG_WARN}sqlite3 not installed"
		return
	fi

	params=( [0]="swupdatestate" [1]="updatechannel" [2]="proxyaddress" [3]="proxyport")
	values=()

	for i in {0..3}; do
		param=${params[$i]}
		value=$(sqlite3 $ZLLDB "select * from config2 where key=\"${param}\"")
		if [ $? -ne 0 ]; then
			return
		fi

		value=$(echo $value | cut -d'|' -f2)

		# basic check for non empty; not set
		if [[ ! -z "$value" ]]; then
			values[$i]=$(echo $value | cut -d'|' -f2)
		fi
	done

	# all parameters found and valid?
	if [ "${#values[@]}" -lt "2" ]; then
		echo "${LOG_DEBUG}found ${#values[*]} parameters expected 2"
		return
	fi

	UPDATE_STATE=${values[0]}
	UPDATE_CHANNEL=${values[1]}
	PROXY_ADDRESS=${values[2]}
	PROXY_PORT=${values[3]}

	# remove old update script if exist
	if [ -e "$USCRIPT" ]; then
		rm $USCRIPT

		if [ $? -ne 0 ]; then
			echo "${LOG_ERROR}failed to removing old update scrip"
			return
		fi
	fi

	if [[ "$LAST_UPDATE_STATE" != "$UPDATE_STATE" ]]; then
		echo "${LOG_INFO}process update state $UPDATE_STATE"
		LAST_UPDATE_STATE=$UPDATE_STATE
	fi

	if [ "$UPDATE_STATE" = "$STATE_TRANSFERRING" ]; then

		curl --head --connect-timeout 30 -k https://www.phoscon.de &> /dev/null
		if [ $? -ne 0 ]; then
			if [[ ! -z "$PROXY_ADDRESS" && ! -z "$PROXY_PORT" && "$PROXY_ADDRESS" != "none" ]]; then
				export http_proxy="http://${PROXY_ADDRESS}:${PROXY_PORT}"
				echo "set proxy: ${PROXY_ADDRESS}:${PROXY_PORT}"
			else
				echo "no internet connection, abort update"
				return
			fi
		fi

		USCRIPT="rpi-deconz-${UPDATE_CHANNEL}.sh"
		UURL="http://deconz.dresden-elektronik.de/raspbian/${USCRIPT}"

		echo "${LOG_DEBUG}download update script"

		cd /tmp

		# get latest update script
		wget $UURL

		if [ $? -ne 0 ]; then
			echo "${LOG_ERROR}failed downloading update script"
			return
		fi

		# Download ok now install
		# todo check signature
		UPDATE_STATE=$STATE_INSTALLING
	fi

	if [ "$UPDATE_STATE" = "$STATE_INSTALLING" ]; then
		echo "${LOG_INFO}start installing update"

		sqlite3 $ZLLDB "update config2 set value='${STATE_INSTALLING}' where key='swupdatestate'" &> /dev/null
		chmod +x $USCRIPT

		./$USCRIPT

		if [ $? -eq 0 ]; then
			echo "${LOG_INFO}finished installing update"
			sqlite3 $ZLLDB "update config2 set value='${STATE_NO_UPDATE}' where key='swupdatestate'" &> /dev/null
			UPDATE_STATE=$STATE_NO_UPDATE
		else
			echo "${LOG_ERROR}error during install update"
			sqlite3 $ZLLDB "update config2 set value='${STATE_READY_INSTALL}' where key='swupdatestate'" &> /dev/null
			# todo report error to deCONZ
			# let user start update again
			UPDATE_STATE=$STATE_READY_INSTALL
		fi

		# done restart update service, so that changes are applied
		systemctl restart deconz-update
	fi
}

while [ 1 ]
do
	checkUpdate
	sleep 10
done
