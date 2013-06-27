#!/bin/sh
# Scan the UK DVB-T broadcasts from the Crystal Palace transmitter
#
# USAGE: ./scan_uk-t.sh > channels.xml
#        ../xml2vdr channels.xml > channels.conf
#
# See http://www.linuxstb.org/dvb-t/ for instructions on how to find
# the frequencies for your local transmitter.
#
# If you are a DVB-T user outside the UK, you will need to amend tune.c
# with the detailed tuning parameters for your country.

# NOTE: CHANGE CARD TO SUIT YOUR SYSTEM - FIRST CARD IS "0", SECOND IS "1" etc

CARD=0
DVBTUNE=../dvbtune

echo '<?xml version="1.0"?>'
echo '<satellite>'
$DVBTUNE -c $CARD -f 490000000 -tm 8 -qam 64 -cr 2_3 -bw 8 -i # Ch 23 - PSB1
$DVBTUNE -c $CARD -f 514000000 -tm 8 -qam 64 -cr 2_3 -bw 8 -i # Ch 26 - PSB2
$DVBTUNE -c $CARD -f 545833333 -tm 8 -qam 256 -cr 2_3 -gi 128 -bw 8 -i # Ch 30- - PSB3/BBCB (DVB-T2)
$DVBTUNE -c $CARD -f 506000000 -tm 8 -qam 64 -cr 3_4 -bw 8 -i # Ch 25 - COM4
$DVBTUNE -c $CARD -f 482000000 -tm 8 -qam 64 -cr 3_4 -bw 8 -i # Ch 22 - COM5
$DVBTUNE -c $CARD -f 529833333 -tm 8 -qam 64 -cr 3_4 -bw 8 -i # Ch 28- - COM6
echo '</satellite>'
