---
title: Plugin: snapd
---

## Introduction

The plugin provides integration with snapd. 

## Update Behavior

When devices such as UEFI DBX are being updated, the snapd plugin will reach out
to snapd and pass the update payload, such that the FDE implemented by snapd can
properly update the TPM policies.

## External Interface Access

This plugin requires:

* access to /run/snapd.socket
* access to /run/snapd-snap.socket

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.
