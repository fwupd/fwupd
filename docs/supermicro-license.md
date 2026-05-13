---
title: Supermicro BMC License
---

## Introduction

While all newer (some X10, all X11 and H12 series) mainboard for Supermicro
support Redfish some features are only available after buying them as
additional feature. One of those are applying BIOS and BMC firmware updates.

## Details

If you want to update your Supermicro board via redfish using fwupd you will
need either the [SFT-OOB-LIC](https://store.supermicro.com/out-of-band-sft-oob-lic.html) or the [SFT-DCMS-Single](https://store.supermicro.com/supermicro-server-manager-dcms-license-key-sft-dcms-single.html) license.

The license can be installed via redfish by POSTing it to
`/redfish/v1/Managers/1/LicenseManager/ActivateLicense`, using the web
interface or using the `contrib/upload-smc-license.py` If the license is not
installed fwupd will add the FWUPD_DEVICE_PROBLEM_MISSING_LICENSE flag to the
device.
