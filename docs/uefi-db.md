---
title: UEFI Secure Boot Certificates
---

## Executive Summary

On the 11th September 2025 a certificate used for signing boot media will expire. Microsoft will not sign updated boot media with the old key, and that **at least one major OEM** is not going to be shipping the expired key on new hardware. This means that existing install media may not boot on some new laptop, desktop and server devices, and that future updates to boot packages may not boot on old hardware.

Microsoft is shipping fixes for select OEMs using Windows Updates automatically. The workaround for Linux is to manually disable secure boot which would be unpopular with anyone that cares about security. Using fwupd is a way that can distribute the updated certificates in Linux.

## Important Terms

* `PK`: “*Platform Key*” – one X509 certificate created by the OEM, e.g. Lenovo
* `KEK`: “*Key Exchange Key*” – multiple certificates (created by Microsoft and the OEM), used to update db, signed by the vendor PK
* `db`: “*Signature Database*” – multiple certificates (created by Microsoft and the OEM), hashes, and signatures, used to allow binaries, signed by KEK
* `dbx`: “*Forbidden Signatures Database*” – multiple certificates, hashes, and signatures, used to block binaries, signed by KEK
* 3rd party certificate – the certificate Microsoft uses to sign non-MS bootloaders that Linux uses, e.g. Shim
  * Windows Production CA 2011 – the old “PCA” certificate
  * Microsoft UEFI CA 2023 (third-party) – the new certificate

## Introduction

The Microsoft certificate which is used for signing “3rd party” boot media (e.g. `shimx64.efi`) will expire on the 11th September 2025. Microsoft has created a new certificate which can be used for signing now. Whilst Microsoft **may** allow us to “dual sign” the shim binary with the old and new certificate until the cut-over date, when the certificate has expired they will only sign shim with the new 3rd party certificate. New laptop, desktop and server products from some OEMs will ship from the factory with **only** the new Microsoft Windows UEFI CA 2023 certificate. This means any shim binaries signed with the old PCA 2011 certificate will not be allowed to run.

This means **it may be impossible to install existing Linux releases on newer machines**. Once the certificate has expired, newly signed `shimx64.efi` binaries will only be signed with the new certificate and thus **will not boot on any existing system that does not have the new certificate installed in the db**. This would potentially mean Linux distributons couldn’t deliver security updates to shim once the old certificate has expired.

Some OEMs have issued BIOS updates to update the KEK and db, but some have instead opted for Microsoft to update the various dbx, db, and KEKs – using new functionality [built into Windows 11](https://techcommunity.microsoft.com/blog/windows-itpro-blog/updating-microsoft-secure-boot-keys/4055324). This means something has to be done for Linux too.

## Solution

At the moment the LVFS distributes `dbx` updates (signed by the Microsoft `KEK`) and users deploy them using fwupd – this has been done over 10M times and with a \>99% success rate. The fwupd  project now needs to distribute two additional artifacts, the **vendor-specific** `KEK` and the **generic** Microsoft `db`. This will likely need backporting into any distribution release that needs to run with Secure Boot turned on when installing onto new hardware. Should backporting the fwupd package be impossible, it is also be possible to build the changes into just Fedora, and then use a `Fedora.iso` LiveUSB to preload the new certificates into the non-volatile machine storage, and *then* install any distribution release that includes the shim signed with the new key. It is not possible to load the expired `db` certificate into a machine with only the new `KEK`.

Updating the KEK and db is a generally safe procedure, with the only limitations being:

* The amount of NVRAM space may be insufficient (or fragmented) – although real-world testing suggests this is a “*failure to install the update*” rather than a “*failure to boot*” scenario. This may be worked around by doing a “factory reset” of the secure boot keys in the BIOS setup
* Some firmware has a toggle to “turn off” the MS 3rd party certificate and the *new* 3rd party cert won’t be matched. Some OEMs are planning firmware updates to add the hash for the new 3rd party certificate. Given that the 3rd party certificate has to be enabled if secure boot is turned on *just to boot Linux* I’m not really worried about accidentally enabling the new certificate as the old one can’t have been turned off.
* Updating the `db` means that the Microsoft Windows BitLocker recovery code may be needed, if the device is dual-booted. It may also cause full disk encryption to not work in the same way on Linux.

## Example of Affected System (Lenovo P50)

    fwupdmgr security
    Host Security ID: HSI:1! (v2.0.9)
    ...
    ✘ UEFI db:                   Invalid

### Affected fwupdmgr security --json

    ...
    {
      "AppstreamId" : "org.fwupd.hsi.Uefi.Db",
      "HsiResult" : "not-valid",
      "Name" : "UEFI db",
      "Description" : "The UEFI db contains the list of valid certificates that can be used to authorize what EFI binaries are allowed to run.",
      "Uri" : "https://fwupd.github.io/libfwupdplugin/hsi.html#org.fwupd.hsi.Uefi.Db",
      "Flags" : [
        "runtime-issue",
        "action-config-fw"
      ]
    },
    ...

### Affected KEK

    <firmware>
      <firmware>
        <id>d5cea02c70cff53bb24bd8cce5035897e565463b</id>
        <issuer>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=Lenovo Ltd. KEK CA 2012</issuer>
        <subject>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=Lenovo Ltd. KEK CA 2012</subject>
      </firmware>
      <firmware>
        <id>b1d0e26aac012618513d33bdb176bbf53962350e</id>
        <issuer>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Corporation Third Party Marketplace Root</issuer>
        <subject>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Corporation KEK CA 2011</subject>
      </firmware>
    </firmware>

### Affected db

    <firmware>
      <firmware>
        <id>7bef7077a4d5017e88764fdbf8d274e74a4411af</id>
        <issuer>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=Lenovo Ltd. Root CA 2012</issuer>
        <subject>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=ThinkPad Product CA 2012</subject>
      </firmware>
      <firmware>
        <id>5e53870688239a03e705b05e4f57c33746db42f9</id>
        <issuer>C=US,ST=North Carolina,O=Lenovo,CN=Lenovo UEFI CA 2014</issuer>
        <subject>C=US,ST=North Carolina,O=Lenovo,CN=Lenovo UEFI CA 2014</subject>
      </firmware>
      <firmware>
        <id>03de12be14ca397df20cee646c7d9b727fcce5f8</id>
        <issuer>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Corporation Third Party Marketplace Root</issuer>
        <subject>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Corporation UEFI CA 2011</subject>
      </firmware>
      <firmware>
        <id>cbbbf4b136db90d11fd37a4a9b2106973aecc095</id>
        <issuer>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Root Certificate Authority 2010</issuer>
        <subject>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Windows Production PCA 2011</subject>
      </firmware>
    </firmware>

## Example of Unaffected Machine (Lenovo ThinkPad P16v)

### Unaffected fwupdmgr security

    Host Security ID: HSI:4 (v2.0.9)
    ...
    ✔ UEFI db:                   Valid

### Unaffected fwupdmgr security --json

    ...
    {
      "AppstreamId" : "org.fwupd.hsi.Uefi.Db",
      "HsiResult" : "valid",
      "Name" : "UEFI db",
      "Description" : "The UEFI db contains the list of valid certificates that can be used to authorize what EFI binaries are allowed to run.",
      "Uri" : "https://fwupd.github.io/libfwupdplugin/hsi.html#org.fwupd.hsi.Uefi.Db",
      "Flags" : [
        "success",
        "runtime-issue",
        "action-config-fw"
      ]
    },
    ...

### Unaffected PK

    <firmware>
      <firmware>
        <id>9aef2123f4de7c19afabd909bb2c8cac4411e07e</id>
        <issuer>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=Lenovo Ltd. PK CA 2012</issuer>
        <subject>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=Lenovo Ltd. PK CA 2012</subject>
      </firmware>
    </firmware>

### Unaffected KEK

    <firmware>
      <flags>always-search</flags>
      <firmware>
        <id>d5cea02c70cff53bb24bd8cce5035897e565463b</id>
        <issuer>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=Lenovo Ltd. KEK CA 2012</issuer>
        <subject>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=Lenovo Ltd. KEK CA 2012</subject>
      </firmware>
      <firmware>
        <id>b1d0e26aac012618513d33bdb176bbf53962350e</id>
        <issuer>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Corporation Third Party Marketplace Root</issuer>
        <subject>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Corporation KEK CA 2011</subject>
      </firmware>
      <firmware>
        <id>4e17021ce9f830eaed13d2817db58a7d9f995838</id>
        <issuer>C=US,O=Microsoft Corporation,CN=Microsoft RSA Devices Root CA 2021</issuer>
        <subject>C=US,O=Microsoft Corporation,CN=Microsoft Corporation KEK 2K CA 2023</subject>
      </firmware>
    </firmware>

### Unaffected db

    <firmware>
      <firmware>
        <id>7bef7077a4d5017e88764fdbf8d274e74a4411af</id>
        <issuer>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=Lenovo Ltd. Root CA 2012</issuer>
        <subject>C=JP,ST=Kanagawa,L=Yokohama,O=Lenovo Ltd.,CN=ThinkPad Product CA 2012</subject>
      </firmware>
      <firmware>
        <id>5e53870688239a03e705b05e4f57c33746db42f9</id>
        <issuer>C=US,ST=North Carolina,O=Lenovo,CN=Lenovo UEFI CA 2014</issuer>
        <subject>C=US,ST=North Carolina,O=Lenovo,CN=Lenovo UEFI CA 2014</subject>
      </firmware>
      <firmware>
        <id>cbbbf4b136db90d11fd37a4a9b2106973aecc095</id>
        <issuer>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Root Certificate Authority 2010</issuer>
        <subject>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Windows Production PCA 2011</subject>
      </firmware>
      <firmware>
        <id>db926014f95ac9ec837442d5d96178538c62434f</id>
        <issuer>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Root Certificate Authority 2010</issuer>
        <subject>C=US,O=Microsoft Corporation,CN=Windows UEFI CA 2023</subject>
      </firmware>
      <firmware>
        <id>03de12be14ca397df20cee646c7d9b727fcce5f8</id>
        <issuer>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Corporation Third Party Marketplace Root</issuer>
        <subject>C=US,ST=Washington,L=Redmond,O=Microsoft Corporation,CN=Microsoft Corporation UEFI CA 2011</subject>
      </firmware>
      <firmware>
        <id>a5b7c551cedc06b94d0c5b920f473e03c2f142f2</id>
        <issuer>C=US,O=Microsoft Corporation,CN=Microsoft RSA Devices Root CA 2021</issuer>
        <subject>C=US,O=Microsoft Corporation,CN=Microsoft UEFI CA 2023</subject>
      </firmware>
    </firmware>
