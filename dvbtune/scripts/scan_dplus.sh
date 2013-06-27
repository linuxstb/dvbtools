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

CARD="0 -D 2"
DVBTUNE=../dvbtune

echo '<?xml version="1.0" encoding="iso-8859-1" ?>'
echo '<satellite>'
$DVBTUNE -c $CARD -s2 -f 10729000 -p v -s 22000 -i
#$DVBTUNE -c $CARD -f 10788000 -p v -s 22000 -i
#$DVBTUNE -c $CARD -f 10818000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 10847000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 10876000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 10979000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 11038000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 11097000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 11156000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 11318000 -p v -s 22000 -i
#$DVBTUNE -c $CARD -s2 -f 11436000 -p v -s 22000 -i
$DVBTUNE -c $CARD -s2 -f 11627000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 11686000 -p v -s 22000 -i
$DVBTUNE -c $CARD -f 11739000 -p v -s 27500 -i
$DVBTUNE -c $CARD -s2 -f 11778000 -p v -s 27500 -i
$DVBTUNE -c $CARD -f 11817000 -p v -s 27500 -i
echo '</satellite>'
