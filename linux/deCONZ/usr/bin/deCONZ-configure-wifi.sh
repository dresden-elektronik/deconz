#!/bin/sh
#
# $1 = mode
# $2 = ssid
# $3 = password
# $4 = channel

HOSTAPD="/etc/hostapd/hostapd.conf"
INTERFACES="/etc/network/interfaces"
DNSMASQ="/etc/dnsmasq.conf"
DHCPCD="/etc/dhcpcd.conf"
DHCPD="/etc/dhcp/dhcpd.conf"
SUPPLICANT="/etc/wpa_supplicant/wpa_supplicant.conf"
MODE=$1

if [[ $MODE == "client" ]]
    then
	    if [[ $# < 3 ]]
            then
	            echo "missing parameter (mode, ssid, password)"
                exit 1
        fi
	else
        if [[ $MODE == "ad-hoc" ]] || [[ $MODE == "accesspoint" ]]
            then
			    if [[ $# != 4 ]]
                    then
	                    echo "missing parameter (mode, ssid, password, channel)"
                        exit 1
                fi
			else
	            echo "wrong parameter mode"
                echo "usage: configure-wifi.sh mode ssid password (channel)"
                echo "mode must be client | ad-hoc | accesspoint"
                exit 1
	    fi
fi

ETH0_INTERFACES="";
## backup and create new interfaces file
if [[ -f $INTERFACES  && ! -f /etc/network/interfaces.original ]]
    then
		## first copy original eth0 settings
		if [[ $MODE == "client" ]]
			then
				ETH0_INTERFACES=$(awk '/iface eth0/{p=1;print;next} /wlan0|wlan1|eth1/{p=0};p' $INTERFACES)
			else
				ETH0_INTERFACES=$(awk '/eth0/{p=1;print;next} /wlan0|wlan1|eth1/{p=0};p' $INTERFACES)
		fi
		## then backup file
        mv $INTERFACES /etc/network/interfaces.original
fi
#if [[ -f $INTERFACES ]]
#    then
		#copy last eth0 settings
		#if [[ $MODE == "client" ]]
		#	then
		#		ETH0_INTERFACES=$(awk '/iface eth0/{p=1;print;next} /wlan0|wlan1|eth1/{p=0};p' $INTERFACES)
		#	else
		#		ETH0_INTERFACES=$(awk '/eth0/{p=1;print;next} /wlan0|wlan1|eth1/{p=0};p' $INTERFACES)
		#fi
#fi
/bin/su -c "echo '# Please note that this file is written to be used with dhcpcd' > $INTERFACES"
/bin/su -c "echo '# For static IP, consult /etc/dhcpcd.conf and man dhcpcd.conf' >> $INTERFACES"
/bin/su -c "echo 'source-directory /etc/network/interfaces.d' >> $INTERFACES"
/bin/su -c "echo 'auto lo' >> $INTERFACES"
/bin/su -c "echo 'iface lo inet loopback' >> $INTERFACES"

#if [[ $ETH0_INTERFACES == "" ]]
#    then
#		if [[ $MODE == "client" ]]
#			then
				/bin/su -c "echo 'iface eth0 inet manual' >> $INTERFACES"
#		fi
#	else
#		/bin/su -c "echo $ETH0_INTERFACES >> $INTERFACES"
#fi

if [[ $MODE == "client" ]]
    then
		/bin/su -c "echo 'allow-hotplug wlan0' >> $INTERFACES"
	    /bin/su -c "echo 'iface wlan0 inet manual' >> $INTERFACES"
        /bin/su -c "echo 'wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf' >> $INTERFACES"
		
		/bin/su -c "echo 'allow-hotplug wlan1' >> $INTERFACES"
	    /bin/su -c "echo 'iface wlan1 inet manual' >> $INTERFACES"
        /bin/su -c "echo 'wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf' >> $INTERFACES"
	
fi

if [[ $MODE == "accesspoint" ]]
    then
		/bin/su -c "echo 'allow-hotplug wlan0' >> $INTERFACES"
	    /bin/su -c "echo 'iface wlan0 inet manual' >> $INTERFACES"
		/bin/su -c "echo 'allow-hotplug wlan1' >> $INTERFACES"
	    /bin/su -c "echo 'iface wlan1 inet manual' >> $INTERFACES"
fi

if [[ $MODE == "ad-hoc" ]]
	then
	    /bin/su -c "echo 'auto wlan0' >> $INTERFACES"
		/bin/su -c "echo 'iface wlan0 inet static' >> $INTERFACES"
		/bin/su -c "echo 'address 192.168.1.$(shuf -i 1-254 -n 1)' >> $INTERFACES"
		/bin/su -c "echo 'netmask 255.255.255.0' >> $INTERFACES"
		/bin/su -c "echo 'wireless-channel $4' >> $INTERFACES"
		/bin/su -c "echo 'wireless-essid $2' >> $INTERFACES"
		/bin/su -c "echo 'wireless-key off' >> $INTERFACES"
		/bin/su -c "echo 'wireless-mode ad-hoc' >> $INTERFACES"
fi

if [[ $MODE == "client" ]]
    then
		## backup and create wpa_supplicant.conf file	
		if [[ -f $SUPPLICANT && ! -f /etc/wpa_supplicant/wpa_supplicant.original ]]
			then
				mv $SUPPLICANT /etc/wpa_supplicant/wpa_supplicant.original
		fi
		/bin/su -c "echo 'ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev' > $SUPPLICANT"
		/bin/su -c "echo 'update_config=1' >> $SUPPLICANT"
		/bin/su -c "echo 'network={' >> $SUPPLICANT"
        /bin/su -c "echo 'ssid=\"$2\"' >> $SUPPLICANT"
        /bin/su -c "echo 'psk=\"$3\"' >> $SUPPLICANT"
        /bin/su -c "echo '}' >> $SUPPLICANT"

	else
		## backup and delete settings in wpa_supplicant.conf file
		if [[ -f $SUPPLICANT && ! -f /etc/wpa_supplicant/wpa_supplicant.original ]]
			then
				mv $SUPPLICANT /etc/wpa_supplicant/wpa_supplicant.original
		fi
		
		/bin/su -c "echo 'network={' > $SUPPLICANT"
        /bin/su -c "echo 'ssid=' >> $SUPPLICANT"
        /bin/su -c "echo 'psk=' >> $SUPPLICANT"
        /bin/su -c "echo '}' >> $SUPPLICANT"
		
		## backup and create hostapd.conf file
		if [[ -f $HOSTAPD && ! -f /etc/hostapd/hostapd.original ]]
			then
				mv $HOSTAPD /etc/hostapd/hostapd.original
		fi

		/bin/su -c "echo interface=wlan0 > $HOSTAPD"
		/bin/su -c "echo #driver=nl80211 >> $HOSTAPD"
		/bin/su -c "echo ssid=$2 >> $HOSTAPD"
		/bin/su -c "echo hw_mode=g >> $HOSTAPD"
		/bin/su -c "echo channel=$4 >> $HOSTAPD"
		/bin/su -c "echo macaddr_acl=0 >> $HOSTAPD"
		/bin/su -c "echo auth_algs=1 >> $HOSTAPD"
		/bin/su -c "echo ignore_broadcast_ssid=0 >> $HOSTAPD"
		/bin/su -c "echo wpa=2 >> $HOSTAPD"
		/bin/su -c "echo wpa_passphrase=$3 >> $HOSTAPD"
		/bin/su -c "echo wpa_key_mgmt=WPA-PSK >> $HOSTAPD"
		/bin/su -c "echo rsn_pairwise=CCMP >> $HOSTAPD"
fi

ETH0_DHCPCD="";
## backup and create dhcpcd.conf file
if [[ -f $DHCPCD && ! -f /etc/dhcpcd.original ]]
    then
		## first copy original eth0 settings
		ETH0_DHCPCD=$(awk '/interface eth0/{flag=1;next}/interface/{flag=0}flag' $DHCPCD)
		## then backup file
        mv $DHCPCD /etc/dhcpcd.original
fi

if [[ -f $DHCPCD ]]
    then
		## copy lasteth0 settings
		ETH0_DHCPCD=$(awk '/interface eth0/{flag=1;next}/interface/{flag=0}flag' $DHCPCD)
fi
/bin/su -c "echo hostname > $DHCPCD"
/bin/su -c "echo clientid >> $DHCPCD"
/bin/su -c "echo persistent >> $DHCPCD"
/bin/su -c "echo option rapid_commit >> $DHCPCD"
/bin/su -c "echo option domain_name_servers, domain_name, domain_search, host_name >> $DHCPCD"
/bin/su -c "echo option classless_static_routes >> $DHCPCD"
/bin/su -c "echo option ntp_servers >> $DHCPCD"
/bin/su -c "echo require dhcp_server_identifier >> $DHCPCD"
/bin/su -c "echo slaac private >> $DHCPCD"
/bin/su -c "echo nohook lookup-hostname >> $DHCPCD"
/bin/su -c "echo $ETH0_DHCPCD >> $DHCPCD"
if [[ $MODE != "client" ]]
    then
		/bin/su -c "echo interface wlan0 >> $DHCPCD"
		/bin/su -c "echo static ip_address=192.168.8.1/24 >> $DHCPCD"
fi

## backup and create dhcpd.conf file
if [[ -f $DHCPD && ! -f /etc/dhcp/dhcpd.original ]]
	then
		mv $DHCPD /etc/dhcp/dhcpd.original
fi
if [[ $MODE == "ad-hoc" ]]
    then
		/bin/su -c "echo ddns-update-style interim\; > $DHCPD"
		/bin/su -c "echo default-lease-time 600\; >> $DHCPD"
		/bin/su -c "echo max-lease-time 7200\; >> $DHCPD"
		/bin/su -c "echo authoritative\; >> $DHCPD"
		/bin/su -c "echo log-facility local7\; >> $DHCPD"
		/bin/su -c "echo subnet 192.168.1.0 netmask 255.255.255.0 { >> $DHCPD"
		/bin/su -c "echo range 192.168.1.50 192.168.1.150\; >> $DHCPD"
		/bin/su -c "echo } >> $DHCPD"
	else
		/bin/su -c "echo '' > $DHCPD"
fi


## backup and create dnsmasq.conf file
if [[ -f $DNSMASQ && ! -f /etc/dnsmasq.original ]]
    then
        mv $DNSMASQ /etc/dnsmasq.original
fi

/bin/su -c "echo interface=wlan0 > $DNSMASQ"
/bin/su -c "echo bind-interfaces >> $DNSMASQ"
/bin/su -c "echo server=8.8.8.8 >> $DNSMASQ"
/bin/su -c "echo domain-needed >> $DNSMASQ"
/bin/su -c "echo bogus-priv >> $DNSMASQ"
/bin/su -c "echo no-dhcp-interface=eth0 >> $DNSMASQ"
/bin/su -c "echo dhcp-range=192.168.8.50,192.168.8.150,24h >> $DNSMASQ"

## restart network

if [[ $MODE == "client" ]]
    then
		service hostapd stop
		update-rc.d hostapd disable 
		ifdown wlan0
        ifup wlan0
	else
	    if [[ $MODE == "ad-hoc" ]]
		    then
			    service hostapd stop
				ifdown wlan0
				ifup wlan0
			else	
		        update-rc.d hostapd enable
				reboot
		fi
fi

exit 0
