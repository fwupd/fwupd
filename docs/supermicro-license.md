---
title: Supermicro BMC License
---

## Introduction

While all newer (some X10, all X11 and H12 series) mainboard for Supermicro
support Redfish some features are only available after buying them as
additional feature. One of those are applying BIOS and BMC firmware updates.

## Details

If you want to update your Supermicro board via redfish using fwupd you will
need either the SFT-OOB-LIC or the SFT-DCMS-Single license.

The license can be installed via redfish by POSTing it to
`/redfish/v1/Managers/1/LicenseManager/ActivateLicense` or using the web
interface. If the license is not installed fwupd will add the
FWUPD_DEVICE_PROBLEM_MISSING_LICENSE flag to the device.
