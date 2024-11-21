#!/bin/bash

OS_ID=$(grep '^VERSION_ID=' /etc/os-release | cut -d '"' -f2)
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
UPDATE_WIFI_IN_DB=false # in some cases it is mandatory to update wifi informations in db (e.g. after a gateway reset)
PROFILE_NAME=""
DECONZ_PROFILE_NAME="deconz_wifi" # standard profile name for deconz managed wifi

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
RECOVER_LAST_WORKING_CONFIGURATION=false

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
	curl --noproxy '*' -s -o /dev/null -d "$@" -X PUT http://127.0.0.1:${DECONZ_PORT}/api/$OWN_PID/config/wifi/updated
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
	
	# check if country code is set
	if [ -z "$(raspi-config nonint get_wifi_country)" ]; then
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}WIFI country not set. Setting default country DE"
		raspi-config nonint do_wifi_country DE
		return
	fi
}

#trap 'on_usr2;' SIGUSR2
#on_usr2() {
#}

# $1 = EVENT
function checkConfig {
    if [[ $1 == $EVENT_PROCESS ]]; then

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
		PROFILE_NAME=$(nmcli -t -f NAME,DEVICE,TYPE connection show | grep '^.*:$WIFI_DEV:802-11-wireless' | cut -d':' -f1)
		USER_SSID=""
		if [ -n "$PROFILE_NAME" ]; then
			USER_SSID=$(nmcli -s -g 802-11-wireless.ssid connection show "$PROFILE_NAME")
		fi		
		SSID_CLIENT="$USER_SSID"
		active=false
		type="client"
		
		# check current state and which files are managed by deCONZ
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}forward current configuration"
		[[ -e "$INTERFACES" && -n $(grep "by deCONZ" $INTERFACES) ]] && mgmt=$((mgmt | mgmt_interfaces))
		[[ $mgmt -ne 0 ]] && CONFIGURED_BY_DECONZ=true
		
		# is active or not?
		if [ -n "$(nmcli connection show --active | grep "$PROFILE_NAME")" ]; then
			active=true
			mgmt=$((mgmt | mgmt_active)) # set active flag
		fi
		PW_ENC=""
		if [ -n "$PROFILE_NAME" ]; then
			PW_ENC=$(nmcli -s -g 802-11-wireless-security.psk connection show $PROFILE_NAME) # set to PW_CLIENT_ENC or PW_AP_ENC
			PW_CLIENT_ENC="$PW_ENC"
			
			# AP or client ?
			if [ $(nmcli -s -g 802-11-wireless.mode connection show "$PROFILE_NAME") -eq "ap" ]; then
				type="accesspoint"
				PW_AP_ENC="$PW_ENC"
				SSID_AP="$USER_SSID"
			fi
		fi						
		
		# check current state and which files are managed by deCONZ
		[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}forward current configuration"
		[[ -e "$INTERFACES" && -n $(grep "by deCONZ" $INTERFACES) ]] && mgmt=$((mgmt | mgmt_interfaces))

		[[ $mgmt -ne 0 ]] && CONFIGURED_BY_DECONZ=true

		if [[ "$type" == "accesspoint" && -n "$SSID_AP" ]]; then
			if [[ $((mgmt & mgmt_interfaces)) -eq 0 ]]; then
				CONFIGURED_BY_DECONZ=false
				putWifiUpdated "{\"status\":\"current-config\", \"type\":\"accesspoint\", \"ssid\":\"$SSID_AP\", \"mgmt\": $mgmt }"
			elif [ "$UPDATE_WIFI_IN_DB" = true ]; then
				putWifiUpdated "{\"status\":\"current-config\", \"wifi\":\"configured\", \"wifitype\":\"accesspoint\", \"wifiname\":\"$SSID_AP\", \"wifipw\":\"$PW_AP\", \"wifipwenc\":\"$PW_AP_ENC\" }"
				UPDATE_WIFI_IN_DB=false
			fi
		fi

		if [[ "$type" == "client" && -n "$SSID_CLIENT" ]]; then
			if [[ $((mgmt & mgmt_interfaces)) -eq 0 ]]; then
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
		# scan for wifis at startup and then ca every 10 minutes or everey 10 seconds if user is at wifi page
		if [ "$WIFI_PAGE_ACTIVE" = true ] || [ $WAIT_WIFI_SCAN -ge 30 ]; then
			WAIT_WIFI_SCAN=0
			scanAvailableWifi
		else
		    WAIT_WIFI_SCAN=$((WAIT_WIFI_SCAN + 1))
		fi

		if [ "$WIFI_CONFIG" = "not-configured" ]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}wifi not configured by deCONZ - cancel"
			LAST_UPDATED="${values[6]}"
			EVENT=$EVENT_PROCESS
			STATE=$STATE_NOT_CONFIGURED
			TIMEOUT=2
			return
		fi

		if [[ "$WIFI_CONFIG" = "deactivated" ]]; then
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
		if [ -z "${values[2]}" ]; then
			[[ $LOG_ERROR ]] && echo "${LOG_ERROR}missing parameter wifiname"
			# try to read active wifi configuration and update db
			UPDATE_WIFI_IN_DB=true
			return
		fi
		if [[ "${values[2]}" == "invalid" ]]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}wifi name is invalid."
			LAST_UPDATED="${values[6]}"
			ERROR_COUNT=$((ERROR_COUNT + 1))
			TIMEOUT=2
			EVENT=$EVENT_PROCESS
			STATE=$STATE_CONNECT_ACCESSPOINT_FAIL
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
				WAIT_RESTORE_CONF=0
				if [[ "$WIFI_TYPE" == *"accesspoint"* ]]; then
					EVENT=$EVENT_PROCESS
					STATE=$STATE_CHECK_ACCESSPOINT
					TIMEOUT=2
					return
				fi
				
				if [[ "$WIFI_TYPE" == "client" ]]; then
					EVENT=$EVENT_PROCESS
					STATE=$STATE_CHECK_CLIENT
					TIMEOUT=2
					return
				fi
			fi
		else
			if [[ "$WIFI_NAME" != "invalid" ]] && [[ "$WIFI_TYPE" != "invalid" ]] && [[ "$WIFI_PW" != "invalid" ]]; then
				if [[ "$WORKING_WIFI_NAME" != "$WIFI_NAME" ]] || [[ "$WORKING_WIFI_TYPE" != "$WIFI_TYPE" ]]; then
					[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}Working configuration differ from user configuration. Count $WAIT_RESTORE_CONF to 40 then try to reconnect to user configuration. Or if working wifi name is empty."
					WAIT_RESTORE_CONF=$((WAIT_RESTORE_CONF + 1))
					if [ $WAIT_RESTORE_CONF -ge 40 ] || [ -z "$WORKING_WIFI_NAME" ] || [ -z "$WORKING_WIFI_TYPE" ]; then
						# 40 x 15s = 10 minutes; then try again to restore configuration wanted by user
						WAIT_RESTORE_CONF=0
						[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}Try to reconnect to user configuration (only if no user is connected to the ap)."
						if [ -z "$(iw dev $WIFI_DEV station dump)" ]; then
							# no user is connected to the ap - try to restore user config
							if [[ "$WIFI_TYPE" == *"accesspoint"* ]]; then
								EVENT=$EVENT_PROCESS
								STATE=$STATE_CHECK_ACCESSPOINT
								TIMEOUT=2
								return
							fi
							
							if [[ "$WIFI_TYPE" == "client" ]]; then
								EVENT=$EVENT_PROCESS
								STATE=$STATE_CHECK_CLIENT
								TIMEOUT=2
								return
							fi		
						fi # restore user config
					fi
				fi
			fi
		fi

		if [[ "$WORKING_WIFI_TYPE" == *"accesspoint"* ]]; then
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
		TIMEOUT=2
		
		### configuration changed?
		if [ -n "$WIFI_PW_ENC" ]; then
			if [[ "$WORKING_WIFI_NAME" != "$WIFI_NAME" ]] || [[ "$WORKING_WIFI_PW_ENC" != "$WIFI_PW_ENC" ]] || [[ "$RECOVER_LAST_WORKING_CONFIGURATION" = true ]]; then
				RECOVER_LAST_WORKING_CONFIGURATION=false
				EVENT=$EVENT_PROCESS
				STATE=$STATE_TRY_CONNECT_ACCESSPOINT
				return
			fi
		else
			if [[ "$WORKING_WIFI_NAME" != "$WIFI_NAME" ]] || [[ "$WORKING_WIFI_PW" != "$WIFI_PW" ]] || [[ "$RECOVER_LAST_WORKING_CONFIGURATION" = true ]]; then
			RECOVER_LAST_WORKING_CONFIGURATION=false
				EVENT=$EVENT_PROCESS
				STATE=$STATE_TRY_CONNECT_ACCESSPOINT
				return
			fi
		fi

		### accesspoint running ?
		if [ -n "$(nmcli connection show --active | grep "$PROFILE_NAME")" ]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}IP Address: $IP_ADDR. iw wlan0 info: $(iw $WIFI_DEV info | grep 'type AP')"
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
				
		EVENT=$EVENT_PROCESS
		STATE=$STATE_CHECK_CONFIG
	fi
}

# $1 = EVENT
function checkClient {
	if [[ $1 == $EVENT_PROCESS ]]; then
		putWifiUpdated '{"status":"check-client"}'
		TIMEOUT=2
		
		### configuration changed?
		if [ -n "$WIFI_PW_ENC" ]; then
			if [[ "$WORKING_WIFI_NAME" != "$WIFI_NAME" ]] || [[ "$WORKING_WIFI_PW_ENC" != "$WIFI_PW_ENC" ]] || [[ "$RECOVER_LAST_WORKING_CONFIGURATION" = true ]]; then
				RECOVER_LAST_WORKING_CONFIGURATION=false
				EVENT=$EVENT_PROCESS
				STATE=$STATE_TRY_CONNECT_CLIENT
				return
			fi
		else
			if [[ "$WORKING_WIFI_NAME" != "$WIFI_NAME" ]] || [[ "$WORKING_WIFI_PW" != "$WIFI_PW" ]] || [[ "$RECOVER_LAST_WORKING_CONFIGURATION" = true ]]; then
				RECOVER_LAST_WORKING_CONFIGURATION=false
				EVENT=$EVENT_PROCESS
				STATE=$STATE_TRY_CONNECT_CLIENT
				return
			fi
		fi
		
		### client running ?
		if [ -n "$(nmcli connection show --active | grep "$WORKING_WIFI_NAME")" ] || [ -n "$(nmcli connection show --active | grep preconfigured)" ]; then
			[[ $LOG_DEBUG ]] && echo "${LOG_DEBUG}IP Address: $IP_ADDR"
			[[ $LOG_INFO ]] && echo "${LOG_INFO}Client running"
			# client running
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
			fi
		fi
		
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
			nmcli radio wifi on
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
	
	curl --noproxy '*' -s -o /dev/null -d "$json" -X PUT http://127.0.0.1:${DECONZ_PORT}/api/$OWN_PID/config/wifi/scanresult

	if [[ "$WIFI_CONFIG" == "deactivated" ]]; then
		nmcli radio wifi off		
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

		nmcli radio wifi off
		WORKING_WIFI_TYPE=""
		WORKING_WIFI_NAME=""
		WORKING_WIFI_PW=""
		WORKING_WIFI_PW_ENC=""

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
		
		WORKING_WIFI_TYPE="$WIFI_TYPE"
		WORKING_WIFI_NAME="$WIFI_NAME"
		WORKING_WIFI_PW="$WIFI_PW"
		WORKING_WIFI_PW_ENC="$WIFI_PW_ENC"
		
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
		
		WORKING_WIFI_TYPE="$WIFI_TYPE"
		WORKING_WIFI_NAME="$WIFI_NAME"
		WORKING_WIFI_PW="$WIFI_PW"
		WORKING_WIFI_PW_ENC="$WIFI_PW_ENC"
		
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

		RECOVER_LAST_WORKING_CONFIGURATION=true
		WIFI_TYPE="$LAST_WIFI_TYPE"
		WIFI_NAME="$LAST_WIFI_NAME"
		if [ -n "$WIFI_PW_ENC" ]; then
			WIFI_PW_ENC="$LAST_WIFI_PW"
		else
			WIFI_PW="$LAST_WIFI_PW"
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
					WIFI_TYPE="accesspoint"
					WIFI_NAME="$AP_BACKUP_NAME"
					WIFI_PW="$AP_BACKUP_PW"
					WIFI_PW_ENC="$AP_BACKUP_PW_ENC"
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
		# remove all ip addresses
		ip addr flush dev $WIFI_DEV
		
		local PW=""			
		if [ -n "$WIFI_PW" ]; then
			PW="$WIFI_PW"
		elif [ -n "$WIFI_PW_ENC" ]; then
			PW="$WIFI_PW_ENC"
		fi
		
		nmcli radio wifi on
		nmcli device disconnect "$WIFI_DEV"
		nmcli device wifi list &> /dev/null
		nmcli device wifi connect "$WIFI_NAME" password "$PW"
		[[ $LOG_INFO ]] && echo "${LOG_INFO}starting wifi client mode"
		putWifiUpdated '{"status":"client-connecting"}'
		
		# check if connection was successfull
		sleep 5
		local ok=0

		for i in {1..5}; do
			local mng=$(iw $WIFI_DEV info | grep "type managed")
			local ssid=$(iw $WIFI_DEV info | grep "ssid $WIFI_NAME")
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
		local AP_PW=""			
		if [ -n "$WIFI_PW" ]; then
			AP_PW="$WIFI_PW"
		elif [ -n "$WIFI_PW_ENC" ]; then
			AP_PW="$WIFI_PW_ENC"
		fi
	
		systemctl stop dnsmasq
		nmcli radio wifi on
		nmcli device disconnect "$WIFI_DEV"
		nmcli connection delete "$DECONZ_PROFILE_NAME"
		nmcli con add con-name "$DECONZ_PROFILE_NAME" ifname wlan0 type wifi ssid "$WIFI_NAME" 
		nmcli con modify "$DECONZ_PROFILE_NAME" wifi-sec.key-mgmt wpa-psk
		nmcli con modify "$DECONZ_PROFILE_NAME" wifi-sec.psk "$AP_PW"
		nmcli con modify "$DECONZ_PROFILE_NAME" 802-11-wireless.mode ap 802-11-wireless.band bg ipv4.method shared
		nmcli connection up "$DECONZ_PROFILE_NAME"
		[[ $LOG_INFO ]] && echo "${LOG_INFO}starting accesspoint"
		putWifiUpdated '{"status":"ap-connecting"}'
		
		# check if accesspoint was created successfully
		for i in {1..5}; do
			sleep 3
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
