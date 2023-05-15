---
title: Plugin: Fibocom
---

## Introduction

This plugin adds support for Fibocom devices.

## GUID Generation

These device use the Fibocom "Firmware Device IDs" as the GUID, e.g.

* `USB\VID_2CB7&PID_01A2`
* `USB\VID_2CB7`

## Vendor ID Security

The vendor ID is set from the USB, for example `USB:0x2CB7`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`
