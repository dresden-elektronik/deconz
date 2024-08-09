#!/bin/bash

HOSTAPD_CONF="/etc/hostapd/hostapd.conf"
INTERFACES="/etc/network/interfaces"
DNSMASQ_CONF="/etc/dnsmasq.conf"
DHCPCD_CONF="/etc/dhcpcd.conf"
WPA_CONF="/etc/wpa_supplicant/wpa_supplicant.conf"
OS_ID=$(cat /etc/os-release | sed -n 's/^ID=\(.*\)/\1/p')
OS_VERSION=$(cat /etc/os-release | sed -n 's/^VERSION="[0-9]\s(\(.*\))"/\1/p')
MAINUSER=$(getent passwd 1000 | cut -d: -f1)
DECONZ_CONF_DIR="/home/$MAINUSER/.local/share"
DECONZ_PORT=0
WIFI_DEV=
WIFI_PAGE_ACTIVE=false #indicates if the wifi page is active in the app
ZLLDB=""
TIMEOUT=0      # main loop iteration timeout, can be controlled by SIGUSR1
OWN_PID=$$
WAIT_RESTORE_CONF=0		# if backup is active wait before restoring configuration wanted by user
WAIT_AP_BACKUP=0 		# wait before creating an ap backup when connection to wifi is lost
WAIT_WIFI_SCAN=30		# if user is not at the wifi page then only scan ca every 10 minutes
SQL_RESULT=
UPDATE_WIFI_IN_DB=false # in some cases it is mandatory to update wifi informations ins db (e.g. after a gateway reset)

# State Machine
readonly STATE_NOT_CONFIGURED=0
readonly STATE_CONFIGURED_CLIENT=1
readonly STATE_CONFIGURED_ACCESSPOINT=2
readonly STATE_CHECK_CONFIG=3
readonly STATE_CHECK_ACCESSPOINT=4
readonly STATE_CHECK_CLIENT=5
readonly STATE_TRY_CONNECT_CLIENT=6
readonly STATE_TRY_CONNECT_ACCESSPOINT=7
readonly STATE_CONNECT_ACCESSPOINT_FAIL=8
readonly STATE_CONNECT_CLIENT_FAIL=9
readonly STATE_LAST_WORKING_CONFIGURATION=10
readonly STATE_ACCESSPOINT_BACKUP=11
readonly STATE_DEACTIVATED=12
STATE=$STATE_CHECK_CONFIG 
ORIGIN_STATE=$STATE_DEACTIVATED

# Events
readonly EVENT_TICK=0
readonly EVENT_PROCESS=1
readonly EVENT_CONFIG_CHANGED=2
EVENT=$EVENT_PROCESS

ERROR_COUNT=0  # counts "connect to wifi" fails
LOG_LEVEL=2

LOG_EMERG=
LOG_ALERT=
LOG_CRIT=
LOG_ERROR=
LOG_WARN=
LOG_NOTICE=
LOG_INFO=
LOG_DEBUG=
LOG_SQL=

[[ $LOG_LEVEL -ge 0 ]] && LOG_EMERG="<0>"
[[ $LOG_LEVEL -ge 1 ]] && LOG_ALERT="<1>"
[[ $LOG_LEVEL -ge 2 ]] && LOG_CRIT="<2>"
[[ $LOG_LEVEL -ge 3 ]] && LOG_ERROR="<3>"
[[ $LOG_LEVEL -ge 4 ]] && LOG_WARN="<4>"
[[ $LOG_LEVEL -ge 5 ]] && LOG_NOTICE="<5>"
[[ $LOG_LEVEL -ge 6 ]] && LOG_INFO="<6>"
[[ $LOG_LEVEL -ge 7 ]] && LOG_DEBUG="<7>"
[[ $LOG_LEVEL -ge 8 ]] && LOG_SQL="<8>"

# format of sqlite3 query result:
# <key>|<value>

WIFI_CONFIG=""
IP_ADDR=""     # ip addr if connection was successfull
ETH0_IP_ADDR=""
LAST_UPDATED="initial_start" # value is not empty so after gateway start gw will connect to given configruation

# user wanted configuration
WIFI_NAME=""   # empty string | Not set | value
WIFI_PW=""
WIFI_TYPE=""   # empty string | value

# last working configuration
LAST_WIFI_TYPE=""
LAST_WIFI_NAME=""
LAST_WIFI_PW=""

# ap backup fallback
AP_BACKUP_NAME=""
AP_BACKUP_PW=""

# encrypted passwords
WIFI_PW_ENC=""
AP_BACKUP_PW_ENC=""
WORKING_WIFI_PW_ENC=""

# actual working configuration
WORKING_WIFI_TYPE=""
WORKING_WIFI_NAME=""
WORKING_WIFI_PW=""

# $1 = queryString
function sqliteSelect() {
    if [[ -z "$ZLLDB" ]] || [[ ! -f "$ZLLDB" ]]; then
        [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}database file not found"
        ZLLDB=
        return
    fi
    [[ $LOG_SQL ]] && echo "SQLITE3 $1"

    SQL_RESULT=$(sqlite3 $ZLLDB "$1")
    if [ $? -ne 0 ]; then
        SQL_RESULT=
    fi
    [[ $LOG_SQL ]] && echo "$SQL_RESULT"
}

function putWifiUpdated() {
	curl --noproxy '*' -s -o /dev/null -d "$@" -X PUT http://127.0.0.1:${DECONZ_PORT}/api/$OWN_PID/config/wifi/updated &
}

function isWiredConnection() {
	if [[ -n `ip a | grep eth0` ]]; then
        local ipaddr=$(ip -o -4 a show dev eth0 up scope global | tr -s '[:blank:]' | cut -d' ' -f4 | cut -d'/' -f1 | head -1)
        if [[ -n "$ipaddr"  ]]; then
            return 0 # true
        fi
    fi
    return 1 # false
}

function encryptWifiPassword() {
	# encrypt wifipw
	if [ -n "$WIFI_PW" ] && [ -n "$WIFI_NAME" ]; then
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting wifi pw"
		local wifipwenc=$(wpa_passphrase "$WIFI_NAME" "$WIFI_PW")
		if [ $? -eq 0 ]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting success"
			WIFI_PW_ENC=$(echo "$wifipwenc" | xargs | cut -d= -f 5 | cut -d' ' -f 1)
			putWifiUpdated "{\"status\":\"current-config\", \"wifipwenc\":\"$WIFI_PW_ENC\" }"
			WIFI_PW=""
		else
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting failed"
		fi
	else
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}do not encrypt wifi pw - missing parameter wifipw or wifiname"
	fi
	# encrypt backup wifipw
	if [ -n "$AP_BACKUP_PW" ] && [ -n "$AP_BACKUP_NAME" ]; then
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting wifi backup pw"
		local wifipwenc=$(wpa_passphrase "$AP_BACKUP_NAME" "$AP_BACKUP_PW")
		if [ $? -eq 0 ]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting success"
			AP_BACKUP_PW_ENC=$(echo "$wifipwenc" | xargs | cut -d= -f 5 | cut -d' ' -f 1)
			putWifiUpdated "{\"status\":\"current-config\", \"wifibackuppwenc\":\"$AP_BACKUP_PW_ENC\" }"
			AP_BACKUP_PW=""
		else
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting failed"
		fi
	else
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}do not encrypt wifi backup pw - missing parameter wifibackuppw or wifibackupname"
	fi
	# encrypt working wifipw
	if [ -n "$WORKING_WIFI_PW" ] && [ -n "$WORKING_WIFI_NAME" ]; then
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting working wifi pw"
		local wifipwenc=$(wpa_passphrase "$WORKING_WIFI_NAME" "$WORKING_WIFI_PW")
		if [ $? -eq 0 ]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting success"
			WORKING_WIFI_PW_ENC=$(echo "$wifipwenc" | xargs | cut -d= -f 5 | cut -d' ' -f 1)
			putWifiUpdated "{\"status\":\"current-config\", \"workingpwenc\":\"$WORKING_WIFI_PW_ENC\" }"
			WORKING_WIFI_PW=""
		else
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}encrypting failed"
		fi
	else
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}do not encrypt working wifi pw - missing parameter workingpw or workingname"
	fi
}

function init {
	local pid=`pidof deCONZ`

	if [ -z "$pid" ]; then
		# deCONZ not running
		ZLLDB=""
		DECONZ_PORT=0
		return
	fi

    # is sqlite3 installed?
	sqlite3 --version &> /dev/null
	if [ $? -ne 0 ]; then
		[[ $LOG_WARN ]] && echo "${LOG_WARN}sqlite3 not installed"
		return
	fi

	# look for latest config in specific order
	drs=("data/dresden-elektronik/deCONZ/zll.db" "dresden-elektronik/deCONZ/zll.db" "deCONZ/zll.db")
	for i in "${drs[@]}"; do
		if [ -f "${DECONZ_CONF_DIR}/$i" ]; then
			ZLLDB="${DECONZ_CONF_DIR}/$i"
			break
		fi

		# fallback when running as root; e.g. in a docker container
		if [ -z "$ZLLDB" ] && [ -d "/root/.local/share/dresden-elektronik" ]; then
			DECONZ_CONF_DIR="/root/.local/share"
			return
		fi
	done

	if [ ! -f "$ZLLDB" ]; then
		# might have been deleted
		ZLLDB=""
	fi

	if [[ -z "$ZLLDB" ]]; then
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}no database file (zll.db) found"
		return
	fi

	# check local TCP ports in LISTEN state of deCONZ process
	# should return websocket and REST-API ports
	for port in `lsof -Panb -i4TCP -w -p $pid -Fn 2> /dev/null | grep "n\*" | cut -d: -f2`
	do
		# probe for REST-API port
		curl -s --noproxy 127.0.0.1 127.0.0.1:$port/api/config | grep bridgeid > /dev/null
		if [ $? -ne 0 ]; then
			continue
		fi

		if [[ "$DECONZ_PORT" != "$port" ]]; then
			DECONZ_PORT=$port
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}found deCONZ port $DECONZ_PORT"
		fi
		break
	done

	if [ $DECONZ_PORT -eq 0 ]; then
		# can't access deCONZ REST-API, force init again
		ZLLDB=""
		return
	fi

	# get wifi interface
	for dev in `ls /sys/class/net`; do
		if [ -d "/sys/class/net/$dev/wireless" ]; then
			WIFI_DEV=$dev
			[[ $LOG_INFO ]] && echo "${LOG_INFO}use wifi interface $WIFI_DEV"
		fi
	done
}

#trap 'on_usr2;' SIGUSR2
#on_usr2() {
#}

# $1 = EVENT
function checkConfig {
    if [[ $1 == $EVENT_PROCESS ]]; then

		local mgmt_hostapd=0x01
		local mgmt_wpa_supplicant=0x02
		local mgmt_interfaces=0x04
		local mgmt_active=0x08
		local mgmt=0
		local CONFIGURED_BY_DECONZ=false
		SSID_AP=""
		SSID_CLIENT=""
		PW_AP=""
		PW_AP_ENC=""
		PW_CLIENT=""
		PW_CLIENT_ENC=""

		# check current state and which files are managed by deCONZ
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}forward current configuration"

		[[ -e "$HOSTAPD_CONF"  && -n $(grep "by deCONZ" $HOSTAPD_CONF) ]] && mgmt=$((mgmt | mgmt_hostapd))
		[[ -e "$WPA_CONF" && -n $(grep "by deCONZ" $WPA_CONF) ]] && mgmt=$((mgmt | mgmt_wpa_supplicant))
		[[ -e "$INTERFACES" && -n $(grep "by deCONZ" $INTERFACES) ]] && mgmt=$((mgmt | mgmt_interfaces))

		[[ $mgmt -ne 0 ]] && CONFIGURED_BY_DECONZ=true

		USER_SSID=$(iw $WIFI_DEV info | grep ssid | sed -e 's/\<ssid\>//g') #active SSID
		if [[ -n "$USER_SSID" ]]; then #  get active configuration
			USER_SSID=$(echo $USER_SSID | xargs) # trim whitespace
			mgmt=$((mgmt | mgmt_active)) # set active flag
			iw $WIFI_DEV info | grep 'type AP' > /dev/null
			if [ $? -eq 0 ]; then
				SSID_AP="$USER_SSID"
				if [ -f "$HOSTAPD_CONF" ]; then
					PW_AP=$(cat $HOSTAPD_CONF | grep wpa_passphrase= -m1 | cut -d'=' -f2)
					PW_AP_ENC=$(cat $HOSTAPD_CONF | grep wpa_psk= -m1 | cut -d'=' -f2)
				fi
			fi

			iw $WIFI_DEV info | grep 'type managed' > /dev/null
			if [ $? -eq 0 ]; then
				SSID_CLIENT="$USER_SSID"
				if [ -f "$WPA_CONF" ]; then
					PW_CLIENT=$(cat $WPA_CONF | grep psk=\" -m1 | cut -d'"' -f2)
					PW_CLIENT_ENC=$(cat $WPA_CONF | grep psk=[^\"] -m1 | cut -d= -f2)
				fi
			fi
		else
			# configured but not active?
			if [ -f "$HOSTAPD_CONF" ]; then
				USER_SSID=$(cat $HOSTAPD_CONF | grep ssid= -m1 | cut -d'=' -f2)
				if [[ -n "$USER_SSID" ]]; then
					SSID_AP="$USER_SSID"
				fi
				PW_AP=$(cat $HOSTAPD_CONF | grep wpa_passphrase= -m1 | cut -d'=' -f2)
				PW_AP_ENC=$(cat $HOSTAPD_CONF | grep wpa_psk= -m1 | cut -d'=' -f2)
				if [ "$UPDATE_WIFI_IN_DB" = true ]; then
					putWifiUpdated "{\"status\":\"current-config\", \"wifi\":\"deactivated\", \"wifitype\":\"accesspoint\", \"wifiname\":\"$SSID_AP\", \"wifipw\":\"$PW_AP\", \"wifipwenc\":\"$PW_AP_ENC\" }"
					UPDATE_WIFI_IN_DB=false
				fi
			fi

			if [ -f "$WPA_CONF" ]; then
				USER_SSID=$(cat $WPA_CONF | grep ssid= -m1 | cut -d'"' -f2)
				if [[ -n "$USER_SSID" ]]; then
					SSID_CLIENT="$USER_SSID"
				fi
				PW_CLIENT=$(cat $WPA_CONF | grep psk=\" -m1 | cut -d'"' -f2)
				PW_CLIENT_ENC=$(cat $WPA_CONF | grep psk=[^\"] -m1 | cut -d= -f2)
				if [ "$UPDATE_WIFI_IN_DB" = true ]; then
					putWifiUpdated "{\"status\":\"current-config\", \"wifi\":\"deactivated\", \"wifitype\":\"client\", \"wifiname\":\"$SSID_CLIENT\", \"wifipw\":\"$PW_CLIENT\", \"wifipwenc\":\"$PW_CLIENT_ENC\" }"
					UPDATE_WIFI_IN_DB=false
				fi
			fi
		fi

		if [[ -n "$SSID_AP" ]]; then
			if [[ $((mgmt & mgmt_hostapd)) -eq 0 ]]; then
				CONFIGURED_BY_DECONZ=false
				putWifiUpdated "{\"status\":\"current-config\", \"type\":\"accesspoint\", \"ssid\":\"$SSID_AP\", \"mgmt\": $mgmt }"
			elif [ "$UPDATE_WIFI_IN_DB" = true ]; then
				putWifiUpdated "{\"status\":\"current-config\", \"wifi\":\"configured\", \"wifitype\":\"accesspoint\", \"wifiname\":\"$SSID_AP\", \"wifipw\":\"$PW_AP\", \"wifipwenc\":\"$PW_AP_ENC\" }"
				UPDATE_WIFI_IN_DB=false
			fi
		fi

		if [[ -n "$SSID_CLIENT" ]]; then
			if [[ $((mgmt & mgmt_wpa_supplicant)) -eq 0 ]]; then
				CONFIGURED_BY_DECONZ=false
				putWifiUpdated "{\"status\":\"current-config\", \"type\":\"client\", \"ssid\":\"$SSID_CLIENT\", \"mgmt\": $mgmt}"
			elif [ "$UPDATE_WIFI_IN_DB" = true ]; then
				putWifiUpdated "{\"status\":\"current-config\", \"wifi\":\"configured\", \"wifitype\":\"client\", \"wifiname\":\"$SSID_CLIENT\", \"wifipw\":\"$PW_CLIENT\", \"wifipwenc\":\"$PW_CLIENT_ENC\" }"
				UPDATE_WIFI_IN_DB=false
			fi
		fi

		if [[ -z "$USER_SSID" ]]; then
			putWifiUpdated "{\"status\":\"current-config\", \"mgmt\":\"$mgmt\"}"
		fi

		#check ip address
		checkIpAddr

		# get database config
		putWifiUpdated '{"status":"check-config"}'
		local params=( [0]="wifi" [1]="wifitype" [2]="wifiname" [3]="wifipw" [4]="wifibackupname" [5]="wifibackuppw" [6]="wifilastupdated" [7]="wifipageactive" [8]="wifipwenc" [9]="wifibackuppwenc")
		local values=()

		for i in {0..9}; do
			param=${params[$i]}
			sqliteSelect "select value from config2 where key=\"${param}\""
			value="$SQL_RESULT"


			# basic check for non empty; not set
			if [[ ! -z "$value" && "$value" != "Not set" ]]; then
				values[$i]=$(echo $value | cut -d'|' -f2)
			fi
		done

		# all parameters found and valid?
		if [ -z "${values[0]}" ]; then
			[[ $LOG_ERROR ]] && echo "${LOG_ERROR}missing parameter wifi"
			return
		fi

		WIFI_CONFIG="${values[0]}"

		if [ -n "${values[7]}" ]; then
			WIFI_PAGE_ACTIVE="${values[7]}"
		fi

		if [ "$WIFI_CONFIG" = "not-available" ]; then
			LAST_UPDATED="${values[6]}"
			EVENT=$EVENT_PROCESS
			STATE=$ORIGIN_STATE
			TIMEOUT=2
			return
		fi

		# scan for wifis
		# scan for wifis at startup and then ca every 10 minutes or everey 20 seconds if user is at wifi page
		if [ "$WIFI_PAGE_ACTIVE" = true ] || [ $WAIT_WIFI_SCAN -ge 30 ]; then
			WAIT_WIFI_SCAN=0
			scanAvailableWifi
		else
		    WAIT_WIFI_SCAN=$((WAIT_WIFI_SCAN + 1))
		fi

		if [ "$WIFI_CONFIG" = "not-configured" ]; then
			#if [[ $CONFIGURED_BY_DECONZ = false ]]; then
				[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}wifi not configured by deCONZ - cancel"
				LAST_UPDATED="${values[6]}"
				EVENT=$EVENT_PROCESS
				STATE=$STATE_NOT_CONFIGURED
				TIMEOUT=2
				return
			#fi
		fi

		if [[ "$WIFI_CONFIG" = "deactivated" ]]; then

			#[[ $CONFIGURED_BY_DECONZ = false ]] && return

			LAST_UPDATED="${values[6]}"
			EVENT=$EVENT_PROCESS
			STATE=$STATE_DEACTIVATED
			TIMEOUT=2
			return
		fi

		if [[ $CONFIGURED_BY_DECONZ = false ]]; then
			if [ "$WIFI_CONFIG" != "new-configured" ]; then
				[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}wifi not configured by deCONZ"
				putWifiUpdated "{\"status\":\"current-config\", \"wifi\":\"not-configured\"}"
				LAST_UPDATED="${values[6]}"
				EVENT=$EVENT_PROCESS
				STATE=$STATE_NOT_CONFIGURED
				TIMEOUT=2
				return
			fi
		fi

		if [ -z "${values[1]}" ]; then
			[[ $LOG_ERROR ]] && echo "${LOG_ERROR}missing parameter wifitype"
			return
		fi
		if [ -z "${values[2]}" ] || [[ "${values[2]}" == "invalid" ]]; then
			[[ $LOG_ERROR ]] && echo "${LOG_ERROR}missing parameter wifiname"
			# try to read active wifi configuration and update db
			UPDATE_WIFI_IN_DB=true
			return
		fi
		if [ -z "${values[3]}" ]; then
			[[ $LOG_WARN ]] && echo "${LOG_WARN}missing parameter wifipw"
			if [ -n "${values[8]}" ]; then
				[[ $LOG_WARN ]] && echo "${LOG_WARN}found encrypted wifipw"
				WIFI_NAME="${values[2]}"
			else
				# try to get wifipw created by old deconz version
				local pwparam="wificlientpw"
				if [[ "${values[1]}" == "accesspoint" ]]; then
					pwparam="wifiappw"
				fi
				sqliteSelect "select value from config2 where key=\"${pwparam}\""
				local oldpw="$SQL_RESULT"
				if [[ -n "$oldpw" ]]; then
					WIFI_PW="$oldpw"
					[[ $LOG_INFO ]] && echo "${LOG_INFO}Found and use wifi password from old deconz wifi version."
					# set value of new parameter wifipw to old value of wifiappw or wificlientpw
					putWifiUpdated "{\"status\":\"current-config\", \"wifipw\":\"$WIFI_PW\"}"
				else
					[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}Error reading parameter ${pwparam} from db"
					return
				fi

				# If param wifipw is missing that means there was an update from old deconz wifi version to new. So set old wifiname.			
				if [[ "${values[1]}" == "client" ]]; then
					# get old param wificlient name from db. If old wifi was accesspoint the param wifiname is the same.
					sqliteSelect "select value from config2 where key='wificlientname'"
					local oldclientname="$SQL_RESULT"
					if [[ -n "$oldclientname" ]]; then
						WIFI_NAME="$oldclientname"
						[[ $LOG_INFO ]] && echo "${LOG_INFO}Found and use wifiname from old deconz wifi version."
						RC=1
						# set value of new parameter wifiname to old value of wificlientname
						putWifiUpdated "{\"status\":\"current-config\", \"wifiname\":\"$WIFI_NAME\"}"
					else
						[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}Error reading parameter wificlientname from db"
						return
					fi
				else
					# old wifi was accesspoint so wifiname is the same parameter
					WIFI_NAME="${values[2]}"
				fi
			fi
		else
			WIFI_NAME="${values[2]}"
			WIFI_PW="${values[3]}"
		fi

		WIFI_TYPE="${values[1]}"

		if [ -n "${values[4]}" ]; then
			AP_BACKUP_NAME="${values[4]}"
		fi

		if [ -n "${values[5]}" ]; then
			AP_BACKUP_PW="${values[5]}"
		fi

		if [ -n "${values[8]}" ]; then
			WIFI_PW_ENC="${values[8]}"
		fi

		if [ -n "${values[9]}" ]; then
			AP_BACKUP_PW_ENC="${values[9]}"
		fi
		# wifi encryption
		encryptWifiPassword

		# client, accesspoint and accesspoint-backup
		if [[ "$WIFI_TYPE" != *"accesspoint"* ]] && [[ "$WIFI_TYPE" != "client" ]] && [[ "$WIFI_TYPE" != "invalid" ]]; then
			[[ $LOG_ERROR ]] && echo "${LOG_ERROR}wrong value $WIFI_TYPE for parameter wifitype"
			return
		fi

		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WORKING_WIFI_TYPE=$WORKING_WIFI_TYPE"
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WORKING_WIFI_NAME=$WORKING_WIFI_NAME"
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WORKING_WIFI_PW=$WORKING_WIFI_PW"
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WORKING_WIFI_PW_ENC=$WORKING_WIFI_PW_ENC"
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WIFI_TYPE=$WIFI_TYPE"
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WIFI_NAME=$WIFI_NAME"
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WIFI_PW=$WIFI_PW"
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WIFI_PW_ENC=$WIFI_PW_ENC"

		# only try to reconnect to user settings if lastupdated has changed
		if [[ "$LAST_UPDATED" != "${values[6]}" ]]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}lastupdated has changed. Connect to new configuration"
			LAST_UPDATED="${values[6]}"
			if [[ "$WIFI_TYPE" = "invalid" ]]; then
				[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}wifi type is invalid. Create an AP backup"
				EVENT=$EVENT_PROCESS
				STATE=$STATE_ACCESSPOINT_BACKUP
				TIMEOUT=2
				return
			else
				WORKING_WIFI_TYPE="$WIFI_TYPE"
				WORKING_WIFI_NAME="$WIFI_NAME"
				WORKING_WIFI_PW="$WIFI_PW"
				WORKING_WIFI_PW_ENC="$WIFI_PW_ENC"
				WAIT_RESTORE_CONF=0
			fi
		else
			if [[ "$WIFI_NAME" != "invalid" ]] && [[ "$WIFI_TYPE" != "invalid" ]] && [[ "$WIFI_PW" != "invalid" ]]; then
				if [[ "$WORKING_WIFI_NAME" != "$WIFI_NAME" ]] || [[ "$WORKING_WIFI_TYPE" != "$WIFI_TYPE" ]]; then
					[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}Working configuration differ from user configuration. Wait until $WAIT_RESTORE_CONF reaches 40 then try to reconnect to user configuration."
					WAIT_RESTORE_CONF=$((WAIT_RESTORE_CONF + 1))
					if [ $WAIT_RESTORE_CONF -ge 40 ] || [ -z "$WORKING_WIFI_NAME" ] || [ -z "$WORKING_WIFI_TYPE" ]; then
						# 40 x 15s = 10 minutes; then try again to restore configuration wanted by user
						WAIT_RESTORE_CONF=0
						[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}Try to reconnect to user configuration (only if no user is connected to the ap)."
						if [ -z "$(iw dev wlan0 station dump)" ]; then
							# no user is connected to the ap - try to restore user config

							# TODO: first scan wifi then try to reconnect

							WORKING_WIFI_TYPE="$WIFI_TYPE"
							WORKING_WIFI_NAME="$WIFI_NAME"
							WORKING_WIFI_PW="$WIFI_PW"
							WORKING_WIFI_PW_ENC="$WIFI_PW_ENC"
						fi
					fi
				fi
			fi
		fi

		if [[ "$WORKING_WIFI_TYPE" = *"accesspoint"* ]]; then
			EVENT=$EVENT_PROCESS
			STATE=$STATE_CHECK_ACCESSPOINT
			TIMEOUT=2
			return
		fi
		if [[ "$WORKING_WIFI_TYPE" == "client" ]]; then
			EVENT=$EVENT_PROCESS
			STATE=$STATE_CHECK_CLIENT
			TIMEOUT=2
			return
		fi
	fi # EVENT_PROCESS
}

# $1 = EVENT
function checkAccesspoint {
	if [[ $1 == $EVENT_PROCESS ]]; then
		putWifiUpdated '{"status":"check-ap"}'
		### accesspoint/accesspoint-backup mode
		if [[ "$WORKING_WIFI_TYPE" = *"accesspoint"* ]]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}enter configure ap section"
			TIMEOUT=2
			which hostapd &> /dev/null
			if [ $? -ne 0 ]; then
				[[ $LOG_CRIT ]] && echo "${LOG_CRIT}hostapd not installed"
				ERROR_COUNT=$((ERROR_COUNT + 1)) # try last working conf then backup
				EVENT=$EVENT_PROCESS
				STATE=$STATE_CONNECT_ACCESSPOINT_FAIL
				return
			fi

			which dnsmasq &> /dev/null
			if [ $? -ne 0 ]; then
				[[ $LOG_CRIT ]] && echo "${LOG_CRIT}dnsmasqd not installed"
				ERROR_COUNT=$((ERROR_COUNT + 1)) # try last working conf then backup
				EVENT=$EVENT_PROCESS
				STATE=$STATE_CONNECT_ACCESSPOINT_FAIL
				return
			fi

			if [ -f "$HOSTAPD_CONF" ]; then
				# check from config
				local OLD_WIFI_NAME=$(cat $HOSTAPD_CONF | grep "^ssid" | cut -d'=' -f2)
				local OLD_WIFI_PW=$(cat $HOSTAPD_CONF | grep "^wpa_passphrase" | cut -d'=' -f2)
				local OLD_WIFI_PW_ENC=$(cat $HOSTAPD_CONF | grep "^wpa_psk" | cut -d'=' -f2)
			fi

			if [ -n "$OLD_WIFI_PW_ENC" ]; then
				if [[ "$WORKING_WIFI_NAME" != "$OLD_WIFI_NAME" ]] || [[ "$WORKING_WIFI_PW_ENC" != "$OLD_WIFI_PW_ENC" ]]; then
					EVENT=$EVENT_PROCESS
					STATE=$STATE_TRY_CONNECT_ACCESSPOINT
					return
				fi
			else
				if [[ "$WORKING_WIFI_NAME" != "$OLD_WIFI_NAME" ]] || [[ "$WORKING_WIFI_PW" != "$OLD_WIFI_PW" ]]; then
					EVENT=$EVENT_PROCESS
					STATE=$STATE_TRY_CONNECT_ACCESSPOINT
					return
				fi
			fi

			### accesspoint running ?
			systemctl -q is-active hostapd
				if [ $? -eq 0 ]; then
					[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}hostapd is running"
					systemctl -q is-active dnsmasq
					if [ $? -eq 0 ]; then
						[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}dnsmasq is running"
						[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}IP Address: $IP_ADDR. iw wlan0 info: $(iw $WIFI_DEV info | grep 'type AP')"
						iw $WIFI_DEV info | grep 'type AP' > /dev/null
						if [[ $? -eq 0 ]] || [[ -n $IP_ADDR ]]; then
							[[ $LOG_INFO ]] && echo "${LOG_INFO}Accesspoint running"
							# accesspoint running
							EVENT=$EVENT_PROCESS
							STATE=$STATE_CONFIGURED_ACCESSPOINT
							return
						else
							# activate ap if not running
							EVENT=$EVENT_PROCESS
							STATE=$STATE_TRY_CONNECT_ACCESSPOINT
							return
						fi
					else
						[[ $LOG_WARN ]] && echo "${LOG_WARN}dnsmasq is not running - reconfigure"
						EVENT=$EVENT_PROCESS
						STATE=$STATE_TRY_CONNECT_ACCESSPOINT
						return
					fi
				else
					[[ $LOG_WARN ]] && echo "${LOG_WARN}hostapd is not running - reconfigure"
					EVENT=$EVENT_PROCESS
					STATE=$STATE_TRY_CONNECT_ACCESSPOINT
					return
				fi
		fi # accesspoint
		EVENT=$EVENT_PROCESS
		STATE=$STATE_CHECK_CONFIG
	fi
}

# $1 = EVENT
function checkClient {
	if [[ $1 == $EVENT_PROCESS ]]; then
		putWifiUpdated '{"status":"check-client"}'
		### client mode
		if [[ "$WORKING_WIFI_TYPE" = "client" ]]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}enter configure client section"
			TIMEOUT=2
			
			which wpa_supplicant &> /dev/null
			if [ $? -ne 0 ]; then
				[[ $LOG_CRIT ]] && echo "${LOG_CRIT}wpa_supplicant not installed"
				ERROR_COUNT=$((ERROR_COUNT + 1)) # try last working conf then backup
				EVENT=$EVENT_PROCESS
				STATE=$STATE_CONNECT_CLIENT_FAIL
				return
			fi

			if [ -f "$WPA_CONF" ]; then
				# check from config
				local OLD_WIFI_NAME=$(cat $WPA_CONF | grep "ssid" | cut -d'=' -f2 | cut -d\" -f2)
				local OLD_WIFI_PW=$(cat $WPA_CONF | grep psk=\" | cut -d'=' -f2 | cut -d\" -f2)
				local OLD_WIFI_PW_ENC=$(cat $WPA_CONF | grep psk=[^\"] -m1 | cut -d= -f2)
			fi

			# reconfigure if parameter changed or services aren't started
			if [[ "$WORKING_WIFI_NAME" != "$OLD_WIFI_NAME" ]] || [[ "$WORKING_WIFI_PW" != "$OLD_WIFI_PW" ]] || [[ "$WORKING_WIFI_PW_ENC" != "$OLD_WIFI_PW_ENC" ]]; then
				EVENT=$EVENT_PROCESS
				STATE=$STATE_TRY_CONNECT_CLIENT
				return
			fi

			### client mode running ?
			pidof wpa_supplicant > /dev/null
			if [ $? -eq 0 ]; then
				[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}wpa_supplicant is running"
				[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}IP Address: $IP_ADDR. Connected to: $(iw $WIFI_DEV info | grep $WORKING_WIFI_NAME)"
				iw $WIFI_DEV info | grep "$WORKING_WIFI_NAME" > /dev/null
				if [[ $? -eq 0 ]] && [[ -n $IP_ADDR ]]; then
					[[ $LOG_INFO ]] && echo "${LOG_INFO}connected to $WORKING_WIFI_NAME"
					# client mode running
					WAIT_AP_BACKUP=0
					EVENT=$EVENT_PROCESS
					STATE=$STATE_CONFIGURED_CLIENT
					return
				else
					# not connected. other wifi down?
					# automatic try reconnect - after 3 minutes: set up AP backup
					WAIT_AP_BACKUP=$((WAIT_AP_BACKUP + 1))
					[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}Not connected. Wait until $WAIT_AP_BACKUP reaches 12 before setting up AP backup."
					if [ $WAIT_AP_BACKUP -ge 12 ]; then
						# 12 x 15s = 3 minutes; then AP backup
						WAIT_AP_BACKUP=0
						[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}other network is more than 3 minutes down. Set up AP backup."
						EVENT=$EVENT_PROCESS
						STATE=$STATE_ACCESSPOINT_BACKUP
						return
						#isWiredConnection
						#if [ $? -ne 0 ]; then
						#	[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}Create an AP backup"
						#	# only create ap backup if no wired connection to the gw is available
						#	EVENT=$EVENT_PROCESS
						#	STATE=$STATE_ACCESSPOINT_BACKUP
						#fi
					fi
				fi
		    else
			    [[ $LOG_WARN ]] && echo "${LOG_WARN}wpa_supplicant is not running - reconfigure network"
				# activate client mode if not running
				EVENT=$EVENT_PROCESS
				STATE=$STATE_TRY_CONNECT_CLIENT
				return
			fi
		fi # client mode
		EVENT=$EVENT_PROCESS
		STATE=$STATE_CHECK_CONFIG
	fi
}

# Scan available wifi and push to REST-API
scanAvailableWifi() {

	[[ -z $WIFI_CONFIG ]] && return
	[[ "$WIFI_CONFIG" == "not-available" ]] && return

	if [[ "$WIFI_CONFIG" == "deactivated" ]]; then
		if [ "$WIFI_PAGE_ACTIVE" = true ]; then
			# activate wifi
			ifconfig wlan0 up
		else
			return
		fi
	fi

	[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}scan available wifi"
	local ssid=""
	local sig=""
	local ch=0
	local i=0

	# scan is processed in a subshell and returns a python array in NETW
	NETW=$(
	iw dev "$WIFI_DEV" scan ap-force |
	while IFS= read -r line
	do
		if [[ $line =~ BSS.*\(on.* ]]; then
			# next record
			mac=""
			ssid=""
			sig=""
			ch=""
			security=""
		fi

		if [[ $line = *"BSS"* ]]; then
			mac=`echo "$line" | cut -d' ' -f2 | xargs | cut -d'(' -f1`
		fi

		if [[ $line = *"SSID:"* ]]; then
			ssid=`echo "$line" | cut -d':' -f2 | xargs`
		fi

		if [[ -z $signal ]] && [[ $line = *"signal:"* ]]; then
			sig=`echo "$line" | cut -d':' -f2 | xargs | cut -d ' ' -f1`
		fi

		if [[ -z $ch ]] && [[ $line = *"primary channel:"* ]]; then
			ch=`echo "$line" | cut -d':' -f2 | xargs`
		fi

		if [[ -z $ch ]] && [[ $line = *"DS Parameter set: channel"* ]]; then
			ch=`echo "$line" | cut -d':' -f2 | xargs | cut -d' ' -f2`
		fi

		if [[ -z $security ]] && [[ $line = *"RSN:"* ]]; then
			security="wpa2"
		fi

		if [[ -n $mac ]] && [[ -n $ssid ]] && [[ -n $sig ]] && [[ -n $ch ]] && [[ -n $security ]]
		then
			if [[ $i -gt 0 ]]; then
				echo -n ", "
			fi
			echo -n "{'mac': '$mac', 'ssid': '$ssid', 'channel': $ch, 'rssi': $sig }"
			mac=""
			ssid=""
			ch=""
			sig=""
			security=""
			i=1
		fi
	done)

	# discard empty result (might be busy)
	[[ -z $NETW ]] && return

	# use python to convert to JSON (incl. unicode stuff)
	local json=$(python -c "import json; print(json.dumps([$NETW]))")
	# -w '%{http_code}'
	curl --noproxy '*' -s -o /dev/null -d "$json" -X PUT http://127.0.0.1:${DECONZ_PORT}/api/$OWN_PID/config/wifi/scanresult &

	if [[ "$WIFI_CONFIG" == "deactivated" ]]; then
			ifconfig wlan0 down
	fi
}

# get ip addr of gateway:
function checkIpAddr {
	[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}check ip address"

	# wifi
	if [[ -n "$WIFI_DEV" ]]; then
		local ip=$(ip -o -4 a show dev $WIFI_DEV up scope global | tr -s '[:blank:]' | cut -d' ' -f4 | cut -d'/' -f1 | head -1)
		if [[ -n "$ip" ]] && [[ "$ip" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}got ip address $ip"
		else
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}no ip address for $WIFI_DEV"
			ip=""
		fi

		#if [[ "$ip" != "$IP_ADDR" ]]; then
			IP_ADDR=$ip
			putWifiUpdated "{\"status\":\"got-ip\",\"ipv4\": \"$IP_ADDR\"}"
		#fi
	fi

	# eth0
	if [[ -n `ip a | grep eth0` ]]; then
		local ip_eth0=$(ip -o -4 a show dev eth0 up scope global | tr -s '[:blank:]' | cut -d' ' -f4 | cut -d'/' -f1 | head -1)
		if [[ -n $ip_eth0 ]] && [[ $ip_eth0 =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}got ip address for eth0 $ip_eth0"
		else
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}no ip address for eth0"
			ip_eth0=""
		fi

		ETH0_IP_ADDR=$ip_eth0
		putWifiUpdated "{\"status\":\"got-ip-eth0\",\"ipv4\": \"$ETH0_IP_ADDR\"}"
	fi
}

# $1 = EVENT
function stateDeactivated {
	if [[ $1 == $EVENT_PROCESS ]]; then
		ORIGIN_STATE=$STATE_DEACTIVATED
		ERROR_COUNT=0
		# deactivate wifi
		putWifiUpdated '{"status":"deactivated"}'
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}wifi deactivated"
		#systemctl -q is-active hostapd
		#if [ $? -eq 0 ]; then
			#echo "${LOG_INFO}hostapd is running - deactivate it"
			systemctl stop hostapd
		#fi
		#systemctl -q is-active wpa_supplicant
		#if [ $? -eq 0 ]; then
			#[[ $LOG_INFO ]] && echo "${LOG_INFO}wpa_supplicant is running - deactivate it"
			#[[ $LOG_INFO ]] && echo "${LOG_INFO}release any dhcp leases"
			systemctl stop wpa_supplicant
			pkill wpa_supplicant
		#fi
		#systemctl -q is-active dhcpcd
		#if [ $? -eq 0 ]; then
		#	echo "${LOG_INFO}dhcpcd is running - deactivate it"
		#	systemctl stop dhcpcd
		#fi
		#systemctl -q is-active dnsmasq
		#if [ $? -eq 0 ]; then
		#	echo "${LOG_INFO}dnsmasq is running - deactivate it"
		#	systemctl stop dnsmasq
		#fi
		# remove all ip addresses
		ip addr flush dev $WIFI_DEV &> /dev/null

		putWifiUpdated "{\"status\":\"current-config\", \"wifi\":\"deactivated\"}"
		STATE=$STATE_CHECK_CONFIG
		EVENT=$EVENT_PROCESS
	fi
}

# $1 = EVENT
function stateNotConfigured {
	if [[ $1 == $EVENT_PROCESS ]]; then
		ERROR_COUNT=0
		putWifiUpdated '{"status":"not-configured"}'
		STATE=$STATE_CHECK_CONFIG
		EVENT=$EVENT_PROCESS
	fi
}

# $1 = EVENT
function configuredAccesspoint {
	if [[ $1 == $EVENT_PROCESS ]]; then
		ORIGIN_STATE=$STATE_CONFIGURED_ACCESSPOINT
		ERROR_COUNT=0
		putWifiUpdated '{"status":"ap-configured"}'
		LAST_WIFI_TYPE="$WORKING_WIFI_TYPE"
		LAST_WIFI_NAME="$WORKING_WIFI_NAME"
		AP_BACKUP_NAME="$WORKING_WIFI_NAME"
		if [ -n "$WORKING_WIFI_PW_ENC" ]; then
			LAST_WIFI_PW="$WORKING_WIFI_PW_ENC"
			AP_BACKUP_PW_ENC="$WORKING_WIFI_PW_ENC"
			putWifiUpdated "{\"status\":\"current-config\", \"workingtype\":\"$WORKING_WIFI_TYPE\", \"workingname\":\"$WORKING_WIFI_NAME\", \"workingpwenc\":\"$WORKING_WIFI_PW_ENC\", \"wifibackuppwenc\":\"$AP_BACKUP_PW_ENC\"}"
		else
			LAST_WIFI_PW="$WORKING_WIFI_PW"
			AP_BACKUP_PW="$WORKING_WIFI_PW"
			putWifiUpdated "{\"status\":\"current-config\", \"workingtype\":\"$WORKING_WIFI_TYPE\", \"workingname\":\"$WORKING_WIFI_NAME\", \"workingpw\":\"$WORKING_WIFI_PW\"}"
		fi
		STATE=$STATE_CHECK_CONFIG
		EVENT=$EVENT_PROCESS
	fi
}

# $1 = EVENT
function configuredClient {
	if [[ $1 == $EVENT_PROCESS ]]; then
		ORIGIN_STATE=$STATE_CONFIGURED_CLIENT
		ERROR_COUNT=0
		putWifiUpdated '{"status":"client-configured"}'
		LAST_WIFI_TYPE="$WORKING_WIFI_TYPE"
		LAST_WIFI_NAME="$WORKING_WIFI_NAME"
		if [ -n "$WORKING_WIFI_PW_ENC" ]; then
			LAST_WIFI_PW="$WORKING_WIFI_PW_ENC"
			putWifiUpdated "{\"status\":\"current-config\", \"workingtype\":\"$WORKING_WIFI_TYPE\", \"workingname\":\"$WORKING_WIFI_NAME\", \"workingpwenc\":\"$WORKING_WIFI_PW_ENC\"}"
		else
			LAST_WIFI_PW="$WORKING_WIFI_PW"
			putWifiUpdated "{\"status\":\"current-config\", \"workingtype\":\"$WORKING_WIFI_TYPE\", \"workingname\":\"$WORKING_WIFI_NAME\", \"workingpw\":\"$WORKING_WIFI_PW\"}"
		fi		
		STATE=$STATE_CHECK_CONFIG
		EVENT=$EVENT_PROCESS
	fi
}

# $1 = EVENT
function connectAccesspointFail {
	if [[ $1 == $EVENT_PROCESS ]]; then
		putWifiUpdated '{"status":"ap-connect-fail"}'
		EVENT=$EVENT_PROCESS
		TIMEOUT=2
		
		isWiredConnection
		if [ $? -eq 0 ]; then
			# gateway has a wired connection to the router
			if [ $ERROR_COUNT -ge 2 ]; then
				# 2 errors: state deactivated
				STATE=$STATE_DEACTIVATED
			elif [ $ERROR_COUNT -ge 1 ]; then
				# 1 error: last working configuration or deactivate
				if [[ "$ORIGIN_STATE" == $STATE_DEACTIVATED ]]; then
					STATE=$STATE_DEACTIVATED
				else
					STATE=$STATE_LAST_WORKING_CONFIGURATION
				fi
			fi
		else
			# only wifi connection exists
			if [ $ERROR_COUNT -ge 2 ]; then
				STATE=$STATE_ACCESSPOINT_BACKUP
			elif [ $ERROR_COUNT -ge 1 ]; then
				STATE=$STATE_LAST_WORKING_CONFIGURATION
			fi
		fi
	fi
}

# $1 = EVENT
function connectClientFail {
	if [[ $1 == $EVENT_PROCESS ]]; then
		putWifiUpdated '{"status":"client-connect-fail"}'
		EVENT=$EVENT_PROCESS
		TIMEOUT=2
		isWiredConnection
		if [ $? -eq 0 ]; then
			# gateway has a wired connection to the router
			if [ $ERROR_COUNT -ge 2 ]; then
				# 2 errors: state deactivated
				STATE=$STATE_DEACTIVATED
			elif [ $ERROR_COUNT -ge 1 ]; then
				# 1 error: last working configuration or deactivate
				if [[ "$ORIGIN_STATE" == $STATE_DEACTIVATED ]]; then
					STATE=$STATE_DEACTIVATED
				else
					STATE=$STATE_LAST_WORKING_CONFIGURATION
				fi
			fi
		else
			# only wifi connection exists
			if [ $ERROR_COUNT -ge 2 ]; then
				# 2 errors: ap backup
				STATE=$STATE_ACCESSPOINT_BACKUP
			elif [ $ERROR_COUNT -ge 1 ]; then
				# 1 errors: last working configuration
				STATE=$STATE_LAST_WORKING_CONFIGURATION
			fi
		fi
	fi
}

# $1 = EVENT
function lastWorkingConfiguration {
	if [[ $1 == $EVENT_PROCESS ]]; then
		putWifiUpdated '{"status":"last-working-config"}'
		[[ $LOG_ERROR ]] && echo "${LOG_ERROR}enable last working configuration"
		TIMEOUT=2
		EVENT=$EVENT_PROCESS

		if [[ -z "$LAST_WIFI_TYPE" ]] || [[ -z "$LAST_WIFI_NAME" ]] || [[ -z "$LAST_WIFI_PW" ]]; then
			isWiredConnection
			if [ $? -eq 0 ]; then
				STATE=$STATE_DEACTIVATED
				return
			else
				STATE=$STATE_ACCESSPOINT_BACKUP
				return
			fi
		fi

		WORKING_WIFI_TYPE="$LAST_WIFI_TYPE"
		WORKING_WIFI_NAME="$LAST_WIFI_NAME"
		if [ -n "$WORKING_WIFI_PW_ENC" ]; then
			WORKING_WIFI_PW_ENC="$LAST_WIFI_PW"
		else
			WORKING_WIFI_PW="$LAST_WIFI_PW"
		fi

		if [[ "$LAST_WIFI_TYPE" == "client" ]]; then
			STATE=$STATE_CHECK_CLIENT
			return
		else
			STATE=$STATE_CHECK_ACCESSPOINT
			return
		fi

		STATE=$STATE_CHECK_CONFIG
		return
	fi
}

# $1 = EVENT
function accesspointBackup {
	if [[ $1 == $EVENT_PROCESS ]]; then
		putWifiUpdated '{"status":"ap-backup"}'
		[[ $LOG_ERROR ]] && echo "${LOG_ERROR}enable accesspoint as backup"		

		EVENT=$EVENT_PROCESS
		TIMEOUT=2

		isWiredConnection
		if [ $? -ne 0 ]; then
			# only set backup if gateway has no wired connection to the router
			if [ -n "$AP_BACKUP_NAME" ]; then
				if [ -n "$AP_BACKUP_PW" ] || [ -n "$AP_BACKUP_PW_ENC" ]; then
					WORKING_WIFI_TYPE="accesspoint"
					WORKING_WIFI_NAME="$AP_BACKUP_NAME"
					WORKING_WIFI_PW="$AP_BACKUP_PW"
					WORKING_WIFI_PW_ENC="$AP_BACKUP_PW_ENC"
					STATE=$STATE_CHECK_ACCESSPOINT
					return
				fi
			fi
		fi
		STATE=$STATE_CHECK_CONFIG
	fi
}

# $1 = EVENT
function tryConnectClient {
	if [[ $1 == $EVENT_PROCESS ]]; then
		## stop network
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}stopping network"

		systemctl stop networking
		systemctl stop dhcpcd
		systemctl stop hostapd
		systemctl stop dnsmasq

		# remove all ip addresses
		ip addr flush dev $WIFI_DEV

		systemctl stop wpa_supplicant
		pkill wpa_supplicant

		[[ $LOG_INFO ]] && echo "${LOG_INFO}configuring WIFI"

		## create wpa_supplicant conf
		if [ -n "$WORKING_WIFI_PW_ENC" ]; then
			echo "# generated by deCONZ
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
country=DE
update_config=1

network={
ssid=\"${WORKING_WIFI_NAME}\"
psk=${WORKING_WIFI_PW_ENC}
}" > $WPA_CONF
		else
			echo "# generated by deCONZ
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
country=DE
update_config=1

network={
ssid=\"${WORKING_WIFI_NAME}\"
psk=\"${WORKING_WIFI_PW}\"
}" > $WPA_CONF	
		fi


if [ "$OS_ID" = "raspbian" ] && [ "$OS_VERSION" = "jessie" ]; then

		echo "# generated by deCONZ
# interfaces(5) file used by ifup(8) and ifdown(8)

# Please note that this file is written to be used with dhcpcd
# For static IP, consult /etc/dhcpcd.conf and 'man dhcpcd.conf'

# Include files from /etc/network/interfaces.d:
source-directory /etc/network/interfaces.d

auto lo
iface lo inet loopback

iface eth0 inet manual

allow-hotplug $WIFI_DEV
iface $WIFI_DEV inet manual
    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf

allow-hotplug wlan1
iface wlan1 inet manual
    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf" > $INTERFACES
fi


# stretch uses dhcpcd hooks for wpa_supplicant
if [ "$OS_ID" = "raspbian" ] && [ "$OS_VERSION" = "stretch" ]; then
			echo "# generated by deCONZ
# interfaces(5) file used by ifup(8) and ifdown(8)

# Please note that this file is written to be used with dhcpcd
# For static IP, consult /etc/dhcpcd.conf and 'man dhcpcd.conf'

# Include files from /etc/network/interfaces.d:
source-directory /etc/network/interfaces.d" > $INTERFACES
fi

		## dynamic ip addresses
			echo "# created by deCONZ
# A sample configuration for dhcpcd.
# See dhcpcd.conf(5) for details.

# Allow users of this group to interact with dhcpcd via the control socket.
#controlgroup wheel

# Inform the DHCP server of our hostname for DDNS.
hostname

# Use the hardware address of the interface for the Client ID.
clientid
# or
# Use the same DUID + IAID as set in DHCPv6 for DHCPv4 ClientID as per RFC4361.
# Some non-RFC compliant DHCP servers do not reply with this set.
# In this case, comment out duid and enable clientid above.
#duid

# Persist interface configuration when dhcpcd exits.
persistent

# Rapid commit support.
# Safe to enable by default because it requires the equivalent option set
# on the server to actually work.
option rapid_commit

# A list of options to request from the DHCP server.
option domain_name_servers, domain_name, domain_search, host_name
option classless_static_routes
# Most distributions have NTP support.
option ntp_servers
# Respect the network MTU. This is applied to DHCP routes.
option interface_mtu

# A ServerID is required by RFC2131.
require dhcp_server_identifier

# Generate Stable Private IPv6 Addresses instead of hardware based ones
slaac private
" > $DHCPCD_CONF

		## restart network
		[[ $LOG_INFO ]] && echo "${LOG_INFO}starting wifi client mode"
		putWifiUpdated '{"status":"client-connecting"}'
		
		# check if wlan0 is softblocked (possible after first start of rpi)
		if [[ -n $(rfkill list wlan | grep yes) ]]; then
			rfkill unblock wlan
			ifconfig wlan0 up
		fi

		#systemctl daemon-reload
		systemctl restart dhcpcd
		systemctl restart networking

		pidof wpa_supplicant > /dev/null
		if [[ $? -ne 0 ]]; then
			# start wpa_supplicant manually, systemd doesn't seem to work
			wpa_supplicant -u -s -i $WIFI_DEV -c $WPA_CONF -B
		fi

		# reload config if already running
		#killall -HUP wpa_supplicant
		sleep 5
		local ok=0

		for i in {1..5}; do
			local mng=$(iw $WIFI_DEV info | grep "type managed")
			local ssid=$(iw $WIFI_DEV info | grep "ssid $WORKING_WIFI_NAME")
			if [[ -n $mng ]] && [[ -n $ssid ]]; then
				# have ip address?
				local ip=$(ip -4 a show dev $WIFI_DEV up scope global)
				[[ -n $ip ]] && ok=1
			fi

			[[ $ok -eq 1 ]] && break
			sleep 2
		done

		if [[ $ok -eq 0 ]]; then
			ERROR_COUNT=$((ERROR_COUNT + 1)) # try last working conf
			[[ $LOG_ERROR ]] && echo "${LOG_ERROR}could not start wifi client mode (error count $ERROR_COUNT)"
			TIMEOUT=2
			EVENT=$EVENT_PROCESS
			STATE=$STATE_CONNECT_CLIENT_FAIL
			return
		fi

		[[ $LOG_INFO ]] && echo "${LOG_INFO}client connection to wifi established"
		checkIpAddr

		EVENT=$EVENT_PROCESS
		STATE=$STATE_CONFIGURED_CLIENT
		TIMEOUT=2
		return
	fi
}

# $1 = EVENT
function tryConnectAccesspoint {
	if [[ $1 == $EVENT_PROCESS ]]; then
		## stop network
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}stopping network"
		systemctl stop dhcpcd
		systemctl stop dnsmasq
		systemctl stop hostapd
		systemctl stop wpa_supplicant
		pkill wpa_supplicant

		# remove all ip addresses
		ip addr flush dev $WIFI_DEV

		[[ $LOG_INFO ]] && echo "${LOG_INFO}configuring AP"

		## create hostapd conf
		if [ -n "$WORKING_WIFI_PW_ENC" ]; then
			echo "# generated by deCONZ
interface=$WIFI_DEV
#driver=nl80211

# AP configuration
ssid=$WORKING_WIFI_NAME
channel=11
hw_mode=g
wmm_enabled=1
country_code=DE
#ieee80211d=1
ignore_broadcast_ssid=0
auth_algs=1

# AP security
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
wpa_psk=$WORKING_WIFI_PW_ENC" > $HOSTAPD_CONF
		else
			echo "# generated by deCONZ
interface=$WIFI_DEV
#driver=nl80211

# AP configuration
ssid=$WORKING_WIFI_NAME
channel=11
hw_mode=g
wmm_enabled=1
#country_code=DE
#ieee80211d=1
ignore_broadcast_ssid=0
auth_algs=1

# AP security
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
wpa_passphrase=$WORKING_WIFI_PW" > $HOSTAPD_CONF
		fi

		## Add conf to hostapd daemon
		sed -i '/DAEMON_CONF=/c\DAEMON_CONF=\"\/etc\/hostapd\/hostapd.conf\"' /etc/default/hostapd

		## Static address for access point
			echo "# generated by deCONZ
# A sample configuration for dhcpcd.
# See dhcpcd.conf(5) for details.

# Allow users of this group to interact with dhcpcd via the control socket.
#controlgroup wheel

# Inform the DHCP server of our hostname for DDNS.
hostname

# Use the hardware address of the interface for the Client ID.
clientid
# or
# Use the same DUID + IAID as set in DHCPv6 for DHCPv4 ClientID as per RFC4361.
# Some non-RFC compliant DHCP servers do not reply with this set.
# In this case, comment out duid and enable clientid above.
#duid

# Persist interface configuration when dhcpcd exits.
persistent

# Rapid commit support.
# Safe to enable by default because it requires the equivalent option set
# on the server to actually work.
option rapid_commit

# A list of options to request from the DHCP server.
option domain_name_servers, domain_name, domain_search, host_name
option classless_static_routes
# Most distributions have NTP support.
option ntp_servers
# Respect the network MTU. This is applied to DHCP routes.
option interface_mtu

# A ServerID is required by RFC2131.
require dhcp_server_identifier

# Generate Stable Private IPv6 Addresses instead of hardware based ones
slaac private

interface $WIFI_DEV
static ip_address=192.168.8.1/24" > $DHCPCD_CONF



# --------------- default for Raspbian Jessi --- /etc/network/interfaces
# interfaces(5) file used by ifup(8) and ifdown(8)

# Please note that this file is written to be used with dhcpcd
# For static IP, consult /etc/dhcpcd.conf and 'man dhcpcd.conf'

# Include files from /etc/network/interfaces.d:
#source-directory /etc/network/interfaces.d

#auto lo
#iface lo inet loopback

#iface eth0 inet manual

#allow-hotplug wlan0
#iface wlan0 inet manual
#    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf

#allow-hotplug wlan1
#iface wlan1 inet manual
#    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf
#----------------------------------------------------------



# --------------- default for Raspbian Stretch --- /etc/network/interfaces
# interfaces(5) file used by ifup(8) and ifdown(8)

# Please note that this file is written to be used with dhcpcd
# For static IP, consult /etc/dhcpcd.conf and 'man dhcpcd.conf'

# Include files from /etc/network/interfaces.d:
#source-directory /etc/network/interfaces.d
#----------------------------------------------------------


if [ "$OS_ID" = "raspbian" ] && [ "$OS_VERSION" = "jessie" ]; then

		echo "# generated by deCONZ
# interfaces(5) file used by ifup(8) and ifdown(8)

# Please note that this file is written to be used with dhcpcd
# For static IP, consult /etc/dhcpcd.conf and 'man dhcpcd.conf'

# Include files from /etc/network/interfaces.d:
source-directory /etc/network/interfaces.d

auto lo
iface lo inet loopback

iface eth0 inet manual

allow-hotplug $WIFI_DEV
iface $WIFI_DEV inet manual
    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf

allow-hotplug wlan1
iface wlan1 inet manual
    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf" > $INTERFACES
fi


# stretch uses dhcpcd hooks for wpa_supplicant
if [ "$OS_ID" = "raspbian" ] && [ "$OS_VERSION" = "stretch" ]; then
			echo "# generated by deCONZ
# interfaces(5) file used by ifup(8) and ifdown(8)

# Please note that this file is written to be used with dhcpcd
# For static IP, consult /etc/dhcpcd.conf and 'man dhcpcd.conf'

# Include files from /etc/network/interfaces.d:
source-directory /etc/network/interfaces.d" > $INTERFACES
fi

		## Configure dns server
		echo "# generated by deCONZ
interface=$WIFI_DEV
no-dhcp-interface=lo,eth0
dhcp-range=192.168.8.50,192.168.8.150,24h
dhcp-option=option:dns-server,192.168.8.1
address=/phoscon.app/192.168.8.1
address=/phoscon.lan/192.168.8.1" > $DNSMASQ_CONF

		# empty wpa_supplicant file
		echo "# generated by deCONZ" > $WPA_CONF

		## restart network
		[[ $LOG_INFO ]] && echo "${LOG_INFO}restarting network"
		putWifiUpdated '{"status":"ap-connecting"}'
		
		# check if wlan0 is softblocked (possible after first start of rpi)
		if [[ -n $(rfkill list wlan | grep yes) ]]; then
			rfkill unblock wlan
			ifconfig wlan0 up
		fi

		systemctl restart networking
		#systemctl daemon-reload
		systemctl restart dhcpcd
		systemctl restart dnsmasq
		systemctl restart hostapd

		for i in {1..5}; do
			sleep 2
			checkIpAddr

			iw $WIFI_DEV info | grep 'type AP' > /dev/null
			if [[ $? -eq 0 ]] && [[ -n $IP_ADDR ]]; then
				[[ $LOG_INFO ]] && echo "${LOG_INFO}Accesspoint created"

				putWifiUpdated "{\"status\":\"current-config\", \"wifi\":\"configured\"}"

				EVENT=$EVENT_PROCESS
				STATE=$STATE_CONFIGURED_ACCESSPOINT
				TIMEOUT=2
				return
			fi
		done

		ERROR_COUNT=$((ERROR_COUNT + 1)) # try last working configuration
		[[ $LOG_ERROR ]] && echo "${LOG_ERROR}could not start accesspoint (error count $ERROR_COUNT)"
		EVENT=$EVENT_PROCESS
		STATE=$STATE_CONNECT_ACCESSPOINT_FAIL	
		TIMEOUT=2
		return
	fi # EVENT_PROCESS
}

# $1 = EVENT
function processEvent {
	case $STATE in
		$STATE_DEACTIVATED)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_DEACTIVATED"
		    stateDeactivated $1
		    ;;
		$STATE_NOT_CONFIGURED)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_NOT_CONFIGURED"
			stateNotConfigured $1
		    ;;
		$STATE_CONFIGURED_CLIENT)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_CONFIGURED_CLIENT"
			configuredClient $1
		    ;;
		$STATE_CONFIGURED_ACCESSPOINT)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_CONFIGURED_ACCESSPOINT"
			configuredAccesspoint $1
		    ;;
		$STATE_CHECK_CONFIG)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_CHECK_CONFIG"
		    checkConfig $1
		    ;;
		$STATE_CHECK_ACCESSPOINT)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_CHECK_ACCESSPOINT"
		    checkAccesspoint $1
		    ;;
		$STATE_CHECK_CLIENT)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_CHECK_CLIENT"
		    checkClient $1
		    ;;
		$STATE_TRY_CONNECT_CLIENT)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_TRY_CONNECT_CLIENT"
			tryConnectClient $1
		    ;;
		$STATE_TRY_CONNECT_ACCESSPOINT)			
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_TRY_CONNECT_ACCESSPOINT"
			tryConnectAccesspoint $1
		    ;;
		$STATE_CONNECT_ACCESSPOINT_FAIL)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_CONNECT_ACCESSPOINT_FAIL"
			connectAccesspointFail $1
		    ;;
		$STATE_CONNECT_CLIENT_FAIL)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_CONNECT_CLIENT_FAIL"
			connectClientFail $1
		    ;;
		$STATE_LAST_WORKING_CONFIGURATION)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_LAST_WORKING_CONFIGURATION"
			lastWorkingConfiguration $1
		    ;;
		$STATE_ACCESSPOINT_BACKUP)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_ACCESSPOINT_BACKUP"
			accesspointBackup $1
		    ;;
		*)
		    [[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}STATE_UNDEFINED"
		    ;;
	esac
}

function checkForNewConfiguration {
	json=$(curl --noproxy '*' -s -X GET http://127.0.0.1:${DECONZ_PORT}/api/$OWN_PID/config/wifi/)
	checkLastUpdated=$(echo $json | grep -o -P 'lastupdated.{0,12}' | grep -o [0-9]*)

	if [ "$checkLastUpdated" != "$LAST_UPDATED" ]; then
		STATE=$STATE_CHECK_CONFIG
		TIMEOUT=0
	elif [[ "$STATE" == "$STATE_DEACTIVATED" ]]; then
		sleep 10
	fi
}

# break loop on SIGUSR1
trap 'TIMEOUT=0' SIGUSR1

while [ 1 ]
do
	if [[ -z "$ZLLDB" ]] || [[ ! -f "$ZLLDB" ]] || [[ $DECONZ_PORT -eq 0 ]]; then
		init
		sleep 2
	fi

	if [[ -z "$ZLLDB" ]] || [[ ! -f "$ZLLDB" ]] || [[ $DECONZ_PORT -eq 0 ]]; then
		sleep 10
		continue
	fi

	while [[ $TIMEOUT -gt 0 ]]
	do
		sleep 1
		checkForNewConfiguration
		TIMEOUT=$((TIMEOUT - 1))
	done

	TIMEOUT=60

	[[ -z "$ZLLDB" ]] && continue
	[[ ! -f "$ZLLDB" ]] && continue
	[[ -z "$WIFI_DEV" ]] && continue

	processEvent $EVENT
done
