#!/bin/bash

trap ctrl_c INT

function ctrl_c() {
    `sudo killall fastforward`
    exit 0
}
if [ $EUID != 0 ]; then
    sudo "$0" "$@"
    exit $?
fi
printf "Assuming bessd is running, NFs are compiled.\n"
printf "Starting NFs for delaying, delay for 1000000000 cycles (~1s)\n"
`sudo $PWD/core/nvport/native_apps/fastforward -i zcvport0 -o zcvport0 -r 1000000000 > /dev/null &`
`sudo $PWD/core/nvport/native_apps/fastforward -i zcvport1 -o zcvport1 -r 1000000000 > /dev/null &`
printf "Starting NFs for dropping, dropping chance 0.5\n"
`sudo $PWD/core/nvport/native_apps/fastforward -i zcvport2 -o zcvport2 -d 50 > /dev/null &`
`sudo $PWD/core/nvport/native_apps/fastforward -i zcvport3 -o zcvport3 -d 50 > /dev/null &`
printf "Starting bessctl\n"
`$PWD/bin/bessctl run manyprobes > /dev/null & `
printf "Done. Ctrl+C to terminate NFs.\n"
while :
do
    sleep 1
done