#!/bin/sh -e
set -e

# these are deprecated in favor of INTERNAL flags
deprecated="FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS
            FWUPD_DEVICE_FLAG_ONLY_SUPPORTED
            FWUPD_DEVICE_FLAG_MD_SET_NAME
            FWUPD_DEVICE_FLAG_MD_SET_VERFMT
            FWUPD_DEVICE_FLAG_NO_GUID_MATCHING
            FWUPD_DEVICE_FLAG_MD_SET_ICON"
for val in $deprecated; do
    if grep -- $val plugins/*/*.c ; then
        exit 1
    fi
done
