---
title: Plugin: Snapd UEFI Integration
---

## Introduction

This plugin allows integration with snapd, to allow db, dbx, KEK and PK updates to be deployed
without the user having to use the volume recovery key on next boot.

## External Interface Access

This plugin requires access to either `/run/snapd-snap.socket` or  `/run/snapd.socket`.

## Version Considerations

This plugin has been available since fwupd version `2.0.19`.
