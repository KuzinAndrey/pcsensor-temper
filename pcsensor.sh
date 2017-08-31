#!/bin/bash

# pcsensor wrapper for Solaris

# libusb_detach_kernel_driver does not seem to work on Solaris.
# Thus, it looks necessary to unload hid driver manually.

hiddriver=/kernel/drv/amd64/hid
moduleid=$( modinfo | awk '$6 == "hid" { print $1 }' )

cd "$( dirname "$0" )" || exit 1

if [ -n "$moduleid" ]; then 
    modunload -i "$moduleid"
fi

./pcsensor "$@"

if [ -n "$moduleid" ]; then 
    modload "$hiddriver"
fi
