---
title: Plugin: Framework QMK
---

## Introduction

This plugin supports the [keyboards of the Framework 16 series](https://frame.work/tw/en/products/keyboard-module).

Devices are updated by triggering a reset via HID, and then entering the Raspberry Pi Pico bootrom.

The bootrom speaks the UF2 protocol (`com.microsoft.uf2`).

## External Interface Access

This plugin requires read/write access to `/dev/hidraw*`.

## Version Considerations

This plugin has been available since fwupd version `2.0.14`.

## Vendor ID Security

The vendor ID is set from the HID vendor ID.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Daniel Schaefer: @JohnAZoidberg
