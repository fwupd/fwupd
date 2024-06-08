---
title: Plugin: UEFI SSP Policy
---

## Introduction

The Microsoft SkuSiPolicy boot-service EFI variables prevent using an insecure Microsoft
bootmgr binary to disable secure boot.

These variables are normally created by Microsoft Windows automatically, but on Linux-only systems
they may be missing.

A local attacker could use an insecure bootmgr binary to disable secure boot and achieve arbitrary
code execution of the early boot process. This would allow running attacker-controlled payloads
even when secure boot is enabled and configured correctly.

This plugin detects those keys and reports the status as an HSI attribute.

See the [https://support.microsoft.com/en-us/topic/kb5027455-guidance-for-blocking-vulnerable-windows-boot-managers-522bb851-0a61-44ad-aa94-ad11119c5e91](knowledge base article) for more information.

## External Interface Access

This plugin requires read access to `/sys/firmware/efi/efivars`.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Jan Setje-Eilers: @jsetje
