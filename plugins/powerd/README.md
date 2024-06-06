---
title: Plugin: Powerd
---

## Introduction

This plugin is used to ensure that some updates are not done on battery power
and that there is sufficient battery power for certain updates that are. It
also provides mutual exclusion between updates and suspend.

## Vendor ID Security

This protocol does not create a device and thus requires no vendor ID set.

## External interface access

This plugin requires access to the `org.chromium.PowerManager` DBus interface.

It provides mutual exclusion between updates and suspend by creating a lockfile
in `${FWUPD_LOCKDIR}/power_override/fwupd.lock`, and checking for the existince
of the file `/run/power_manager/power/suspend_announced`.

## Version Considerations

This plugin has been available since fwupd version `1.6.2`.
