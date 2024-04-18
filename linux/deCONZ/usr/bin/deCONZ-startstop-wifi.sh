#!/bin/sh
#
# $1 = mode
# $2 = start/stop/restore

SUPPLICANT="/etc/wpa_supplicant/wpa_supplicant.conf"

#original config files
HOSTAPD_ORIGINAL="/etc/hostapd/hostapd.original"
INTERFACES_ORIGINAL="/etc/network/interfaces.original"
DNSMASQ_ORIGINAL="/etc/dnsmasq.original"
DHCPCD_ORIGINAL="/etc/dhcpcd.original"
DHCPD_ORIGINAL="/etc/dhcp/dhcpd.original"
SUPPLICANT_ORIGINAL="/etc/wpa_supplicant/wpa_supplicant.original"

if [[ $# != 2 ]]
	then
		echo "missing parameter (mode startstop)"
		exit 1
fi

if [[ $1 != "client" && $1 != "ad-hoc" && $1 != "accesspoint" ]]
	then
		echo "wrong parameter mode [client | ad-hoc | accesspoint]"
		exit 1
fi

if [[ $2 != "start" && $2 != "stop" && $2 != "restore" ]]
	then
		echo "wrong parameter startstop [start | stop | restore]"
		exit 1
fi

MODE=$1
STARTSTOP=$2

## restore original raspberry pi settings
if [[ $STARTSTOP == "restore" ]]
    then
		if [[ -f  $HOSTAPD_ORIGINAL ]]
			then
				sudo mv $HOSTAPD_ORIGINAL /etc/hostapd/hostapd.conf
		fi
		if [[ -f  $INTERFACES_ORIGINAL ]]
			then
				sudo mv $INTERFACES_ORIGINAL /etc/network/interfaces
		fi
		if [[ -f  $DNSMASQ_ORIGINAL ]]
			then
				sudo mv $DNSMASQ_ORIGINAL /etc/dnsmasq.conf
		fi
		if [[ -f  $DHCPCD_ORIGINAL ]]
			then
				sudo mv $DHCPCD_ORIGINAL /etc/dhcpcd.conf
		fi
		if [[ -f  $DHCPD_ORIGINAL ]]
			then
				sudo mv $DHCPD_ORIGINAL /etc/dhcp/dhcpd.conf
		fi
		if [[ -f  $SUPPLICANT_ORIGINAL ]]
			then
				sudo mv $SUPPLICANT_ORIGINAL /etc/wpa_supplicant/wpa_supplicant.conf
		fi
		
		service hostapd stop
		update-rc.d hostapd disable
						
		sudo reboot
		exit 0
fi

## start or stop wifi
if [[ $MODE == "client" ]]
    then
		if [[ $STARTSTOP == "start" ]]
			then
				ifdown wlan0
				ifup wlan0
				ifconfig wlan0 | grep 'inet addr:' | cut -d: -f2 | cut -d' ' -f1
		fi
		if [[ $STARTSTOP == "stop" ]]
			then
				ifdown wlan0
				/bin/su -c "echo 'network={' > $SUPPLICANT"
				/bin/su -c "echo 'ssid=' >> $SUPPLICANT"
				/bin/su -c "echo 'psk=' >> $SUPPLICANT"
				/bin/su -c "echo '}' >> $SUPPLICANT"
		fi		
		
	else
	    if [[ $MODE == "accesspoint" ]]
		    then
				if [[ $STARTSTOP == "start" ]]
					then
						service hostapd stop
						sleep 3
						service hostapd start
						update-rc.d hostapd enable
						ifconfig wlan0 | grep 'inet addr:' | cut -d: -f2 | cut -d' ' -f1
				fi
				if [[ $STARTSTOP == "stop" ]]
					then
						service hostapd stop
						update-rc.d hostapd disable
				fi
			else
			    if [[ $STARTSTOP == "start" ]]
					then
						service hostapd stop
						ifdown wlan0
						ifup wlan0
						ifconfig wlan0 | grep 'inet addr:' | cut -d: -f2 | cut -d' ' -f1
				fi
				if [[ $STARTSTOP == "stop" ]]
					then
						service hostapd stop
						ifdown wlan0
				fi
		fi
fi

exit 0