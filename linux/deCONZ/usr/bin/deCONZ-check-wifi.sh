#!/bin/sh
#
# exit 1 = wlan0 down
# exit 2 = some files are missing or incomplete
# exit 3 = dnsmasq, wpa_supplicant and or hostapd not installed

HOSTAPD="/etc/hostapd/hostapd.conf"
INTERFACES="/etc/network/interfaces"
DNSMASQ="/etc/dnsmasq.conf"
DHCPCD="/etc/dhcpcd.conf"
DHCPD="/etc/dhcp/dhcpd.conf"
WPA_SUPP="/etc/wpa_supplicant/wpa_supplicant.conf"
ERROR=0
MODE=accesspoint
SSID=""
CHANNEL=1
PASSWORD=""
IP=192.168.8.1

if [[ ! $(which hostapd) ]]
    then
        echo "hostapd_not_installed"
fi

if [[ ! $(which dnsmasq) ]]
    then
        echo "dnsmasq_not_installed"
fi

if [[ ! $(which wpa_supplicant) ]]
    then
        echo "wpa_supplicant_not_installed"
fi

if [[ ! $(which dhcpcd) ]]
    then
        echo "dhcpcd_not_installed"
fi

if ! [[ $(which hostapd) && $(which dnsmasq) && $(which wpa_supplicant) && $(which dhcpcd) ]]
    then
        exit 3
fi

## check interfaces
if [[ -f $INTERFACES ]]
    then
        if [[ $(cat $INTERFACES | grep -E '^wireless-mode ad-hoc$') ]]
            then
                ## wireless mode is ad-hoc
                MODE=ad-hoc
        fi
		# TODO: also allow whitespace before wpa-conf and check that line is not comment
        if [[ $(cat $INTERFACES | grep -E '^iface wlan0 inet manual$') && $(cat $INTERFACES | grep -E '^wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf$') ]]
            then
                ## wireless mode is client
                MODE=client
        fi
		if [[ $MODE == "accesspoint" ]]
		    then
				if [[ ! $(cat $INTERFACES | grep -E '^iface wlan0 inet manual') ]]
					then
						## manual needed for configuration with dhcpcd.conf
						echo "interfaces_not_configured"
						echo "no inet manual"
						ERROR=2
				fi
		fi
		if [[ $MODE == "ad-hoc" ]]
		    then
				if [[ ! $(cat $INTERFACES | grep -E '^iface wlan0 inet static$') ]]
					then
						## static needed for configuration of ad-hoc
						echo "interfaces_not_configured"
						ERROR=2
					else
					    if [[ $(cat $INTERFACES | grep -cE "^address [0-9]+.[0-9]+.[0-9]+.[0-9]+$") == 1 ]]
							then
							    # address just exist once
								IP=$(cat $INTERFACES | grep "address" | cut -d' ' -f2)
							else
								## interfaces not configured
								echo "interfaces_not_configured"
								ERROR=2
						fi
						if [[ $(cat $INTERFACES | grep -E "^wireless-essid") ]]
							then
								SSID=$(cat $INTERFACES | grep -E "^wireless-essid" | cut -d' ' -f2)
							else
								## interfaces not configured
								echo "interfaces_not_configured"
								ERROR=2
						fi
						if [[ $(cat $INTERFACES | grep -E "^wireless-channel") ]]
							then
								CHANNEL=$(cat $INTERFACES | grep -E "^wireless-channel" | cut -d' ' -f2)
							else
								## interfaces not configured
								echo "interfaces_not_configured"
								ERROR=2
						fi
				fi
		fi
		if [[ $MODE == "client" ]]
		    then
				if [[ ! $(cat $INTERFACES | grep -E '^iface wlan0 inet manual$') ]]
					then
						## manual needed for client mode
						echo "interfaces_not_configured"
						echo "no inet manual"
						ERROR=2
				fi
		fi
    else
        ## no interfaces file
        echo "no_file_interfaces"
        ERROR=2
fi

## check dhcpcd
if [[ -f $DHCPCD ]]
    then
		if [[ $MODE == "accesspoint" ]]
		    then 
				if [[ ! $(cat $DHCPCD | grep -E '^interface wlan0$') ]]
					then
						## dhcpcd not configured
						echo "dhcpcd_not_configured"
						echo "no interface wlan0"
						ERROR=2
					else
						if [[ $(cat $DHCPCD | grep -E "static ip_address=[0-9]+.[0-9]+.[0-9]+.[0-9]+/[1-9][0-9]*") ]]
							then
								IP=$(cat $DHCPCD | grep "static ip_address" | cut -d= -f2 | cut -d/ -f1)
							else
								## dhcpcd not configured
								echo "dhcpcd_not_configured"
								echo "no static ip_address"
								ERROR=2
						fi
				fi
		fi
    else
        ## no dhcpcd.conf file
        echo "no_file_dhcpcd"
        ERROR=2
fi

## check dhcpd
if [[ -f $DHCPD ]]
    then
		if [[ $MODE == "ad-hoc" ]]
		    then 
				if [[ $(cat $DHCPD | grep "subnet") && $(cat $DHCPD | grep "netmask") && $(cat $DHCPD | grep "range") ]]
					then
					    SUBNET=$(cat ${DHCPD} | grep "subnet" | cut -d' ' -f2)
						NETMASK=$(cat ${DHCPD} | grep "netmask" | cut -d' ' -f4)
						RANGE_FROM=$(cat ${DHCPD} | grep range | cut -d' ' -f2)
						RANGE_TO=$(cat ${DHCPD} | grep range | cut -d' ' -f2 | cut -d\; -f1)
						if [[ $(echo $SUBNET | cut -d. -f-3) != $(echo $IP | cut -d. -f-3) ||
						      $NETMASK != 255.255.255.0 ||
						      $(echo $IP | cut -d. -f-3) != $(echo $RANGE_FROM | cut -d. -f-3) ||
							  $(echo $IP | cut -d. -f-3) != $(echo $RANGE_TO | cut -d. -f-3) ]]
							then
								## dhcp-range and ip address don't match
								echo "dhcpd_not_configured"
								ERROR=2 
						fi
					else
						## missing parameters
						echo "dhcpd_not_configured"
						ERROR=2 
				fi
		fi
    else
        ## no dhcpd.conf file
        echo "no_file_dhcpd"
        ERROR=2
fi

## check hostapd
if [[ -f $HOSTAPD ]]
    then
	    if [[ $MODE == "client" ]]
		    then
			    ## ensure that hostapd is disabled
			    if [[ ! $(service hostapd status | grep 'inactive (dead)') ]]
		            then
						echo "hostapd_not_configured"
						echo "hostapd is still enabled"
			            ERROR=2
				fi
			else
			    if [[ $MODE == "accesspoint" ]]
		            then
			            ## ensure that hostapd is running
			            if [[ $(service hostapd status | grep 'inactive (dead)') ]]
		                    then
								echo "hostapd_not_configured"
								echo "hostapd is not running"
			                    ERROR=2
				        fi
		        fi
		fi
		if [[ ! $(cat $HOSTAPD | grep -E '^wpa_passphrase=') ]]
			then
			    if [[ $MODE == "accesspoint" ]]
			        then
				        ## no wifi pw set
				        echo "hostapd_not_configured"
						echo "no wpa_passphrase"
				        ERROR=2
				fi
		fi
		# always show ap pw
		PASSWORD=$(cat $HOSTAPD | grep -E '^wpa_passphrase=' | cut -f2 -d=)
		if [[ $MODE == "accesspoint" ]]
			then
				if ! [[ $(cat $HOSTAPD | grep -E '^wpa_passphrase=') && $(cat $HOSTAPD | grep -E '^interface=wlan0$') && $(cat $HOSTAPD | grep -E '^wpa=2$') && $(cat $HOSTAPD | grep -E '^wpa_key_mgmt=WPA-PSK$') ]]
					then
						## missing parameters
						echo "hostapd_not_configured"
						echo "no wpa_passphrase, interface=wlan0, wpa=2, wpa_key_mgmt"
						ERROR=2 
				fi
				if [[ ! $(cat $HOSTAPD | grep -E '^ssid=') ]]
					then
						## no wifi name set
						echo "hostapd_not_configured"
						echo "no ssid"
						ERROR=2
					else
						SSID=$(cat $HOSTAPD | grep -E '^ssid=' | cut -f2 -d=)
				fi
				if [[ ! $(cat $HOSTAPD | grep -E '^channel=') ]]
					then
						## no wifi channel set
						echo "hostapd_not_configured"
						echo "no channel"
						ERROR=2
					else
						CHANNEL=$(cat $HOSTAPD | grep -E '^channel=' | cut -f2 -d=)
				fi
		fi
    else
        ## hostapd.conf file
        echo "no_file_hostapd"
        ERROR=2
fi

## check dnsmasq
if [[ -f $DNSMASQ ]]
    then
		if [[ $MODE == "accesspoint" ]]
			then
				if [[ ! $(cat $DNSMASQ | grep -E '^interface=wlan0') ]]
					then
						## missing parameters
						echo "dnsmasq_not_configured"
						echo "no interface wlan0"
						ERROR=2 
				fi
				if [[ $(cat $DNSMASQ | grep -E '^dhcp-range=') ]]
					then
						DHCP_IP_FROM=$(cat /etc/dnsmasq.conf | grep dhcp-range= | cut -d= -f2 | cut -d, -f1)
						DHCP_IP_TO=$(cat /etc/dnsmasq.conf | grep dhcp-range= | cut -d= -f2 | cut -d, -f2)
						if [[ $(echo $IP | cut -d. -f-3) != $(echo $DHCP_IP_FROM | cut -d. -f-3) || $(echo $IP | cut -d. -f-3) != $(echo $DHCP_IP_TO | cut -d. -f-3) ]]
							then
								## dhcp-range and ip address don't match
								echo "dnsmasq_not_configured"
								echo "dhcp-range missmatch"
								ERROR=2 
						fi
					else
						## missing parameters
						echo "dnsmasq_not_configured"
						echo "no dhcp-range"
						ERROR=2 
				fi
		fi
	else
        ## no dnsmasq.conf file
        echo "no_file_dnsmasq"
        ERROR=2
fi

## check wpa_supplicant
if [[ -f $WPA_SUPP ]]
    then
		if [[ $MODE == "client" ]]
			then
				if [[ ! $(cat $WPA_SUPP | grep -E '^network=') ]]
					then
						## missing parameters
						echo "wpa_supplicant_not_configured"
						echo "no network parameter"
						ERROR=2 
					else
						if [[ ! $(cat $WPA_SUPP | grep -E '^ssid="') ]]
							then
								## missing parameters
								echo "wpa_supplicant_not_configured"
								echo "no ssid"
								ERROR=2 
							else
								SSID=$(cat $WPA_SUPP | grep -E '^ssid="' | cut -f2 -d\")
								if [[ $SSID == "" ]]
									then
										## missing parameters
										echo "wpa_supplicant_not_configured"
										echo "ssid is empty"
										ERROR=2
								fi
						fi
						if [[ ! $(cat $WPA_SUPP | grep -E '^psk=') ]]
							then
								## missing parameters
								echo "wpa_supplicant_not_configured"
								echo "no psk"
								ERROR=2 
							else
								PSK=$(cat $WPA_SUPP | grep psk= | cut -f2 -d=)
								if [[ $PSK == "" ]]
									then
										## missing parameters
										echo "wpa_supplicant_not_configured"
										echo "psk is empty"
										ERROR=2
								fi
						fi
				fi
		fi
	else
        ## no wpa_supplicant.conf file
        echo "no_file_wpa_supplicant"
        ERROR=2
fi


## some files are missing or incomplete
if [[ $ERROR == 2 ]]
    then
        exit 2
fi

if [[ $(ifconfig wlan0 | grep "inet addr") ]]
    then
	    ## wifi is running
        echo "wifi_running"
        echo "mode_${MODE}/mode"
        echo "ssid_${SSID}/ssid"
		echo "pw_${PASSWORD}/pw"
        echo "channel_${CHANNEL}/channel"
		IP=$(ifconfig wlan0 | grep 'inet addr:' | cut -d: -f2 | cut -d' ' -f1)
		echo "ip_${IP}/ip"
        exit 0
	else
		## everything is configured but wlan0 is down
        echo "wlan0_down"
        echo "mode_${MODE}/mode"
        echo "ssid_${SSID}/ssid"
		echo "pw_${PASSWORD}/pw"
        echo "channel_${CHANNEL}/channel"
		exit 1
fi
