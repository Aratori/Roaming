#!bin/bash

sudo service network-manager stop

sudo pkill nm-apple

sudo ip link set wlan0 up

sudo iwconfig wlan0

sudo wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf

sudo dhclient wlan0
