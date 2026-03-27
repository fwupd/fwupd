---
title: Device Version Blocklist
---

## Introduction

There may be circumstances where the end user or enterprise wants to artificially restrict the
minimum or maximum version of firmware that can be deployed to the device.
This could be where a new version of firmware drops a needed feature, fixes a security issue that
is actually a required feature in production or to prevent a known-broken firmware being deployed.

## Implementation

To prevent downgrade the `VersionLowest` key can be used in a quirk file, and to prevent upgrade
the `VersionHighest` key can be used instead. They can also be used together if needed.

We can write a new quirk file either into `/usr/share/fwupd/quirks.d` or `/var/lib/fwupd/quirks.d`
for immutable systems. e.g. create a file `limit.quirk` in either location matching the chosen
instance ID (`USB\VID_273F&PID_1004`) or GUID (`2fa8891f-3ece-53a4-adc4-0dd875685f30`).

This file can then be deployed using Ansible to all machines as required.

### Prevent Version Downgrade

To prevent installing any lower version than 2.0.1:

    [USB\VID_273F&PID_1004]
    VersionLowest = 2.0.1

### Prevent Version Upgrade

To prevent installing any higher version than 2.0.1:

    [USB\VID_273F&PID_1004]
    VersionHighest = 2.0.1

### Prevent Version Change

To prevent installing any lower or higher version than 2.0.1:

    [USB\VID_273F&PID_1004]
    VersionLowest = 2.0.1
    VersionHighest = 2.0.1

### Skip Current Upgrade

To prevent upgrading from 2.0.1 to 2.0.2, but allowing 2.0.3 and newer:

    [USB\VID_273F&PID_1004]
    VersionLowest = 2.0.3
