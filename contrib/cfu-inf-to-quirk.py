#!/usr/bin/python3
#
# Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=invalid-name,missing-docstring

import sys


def _convert_inf_to_quirk(fn: str) -> None:
    with open(fn, "rb") as f:
        lines = f.read().decode().split("\n")
    instance_id = None
    comment = None
    dict_convert = {
        "VersionsFeatureValueCapabilityUsageRangeMinimum": "CfuVersionGetReport",
        "OfferOutputValueCapabilityUsageRangeMinimum": "CfuOfferSetReport",
        "OfferInputValueCapabilityUsageRangeMinimum": "CfuOfferGetReport",
        "PayloadOutputValueCapabilityUsageRangeMinimum": "CfuContentSetReport",
        "PayloadInputValueCapabilityUsageRangeMinimum": "CfuContentGetReport",
    }
    is_cfu = False
    data = {"Plugin": "cfu"}
    for line in lines:
        line = line.split(";")[0]
        if line.find("HidCfu.NT.Services") != -1:
            is_cfu = True
        if line.find("FwUpdateFriendlyName") != -1:
            try:
                comment = line.split("=")[1]
                for token in ['"', "Firmware Update", "(TM)"]:
                    comment = comment.replace(token, "")
                comment = comment.strip()
            except IndexError:
                pass
        if line.find("HID\\VID_") != -1:
            instance_id = line.split(",")[1].strip().upper()
            instance_id = instance_id.replace("HID\\", "USB\\")
            instance_id = "&".join(instance_id.split("&")[:2])
        for inf_key, quirk_key in dict_convert.items():
            if line.find(inf_key) != -1:
                data[quirk_key] = line.split(",")[4].strip()
    if is_cfu:
        if comment:
            print(f"# {comment}")
        print(f"[{instance_id}]")
        for key, value in data.items():
            print(f"{key} = {value}")
    else:
        sys.exit("this is not a CFU firmware.inf")


for fn in sys.argv[1:]:
    _convert_inf_to_quirk(fn)
