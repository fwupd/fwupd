---
title: Best Known Configuration
---

## Introduction

Component `<tag>`s are used both by OEMs and by customers to identify a *known-working* (or
commercially supported) set of firmware on the machine.

This allows two things:

* Factory recovery where a system in the field has been upgraded
* Ensuring a consistent set of vendor-tested firmware for a specific workload

The tags are either assigned in the firmware cabinet archive (the `.metainfo.xml` file) or added
post-upload on the LVFS and are then included in the public AppStream metadata.
A single firmware can be marked with multiple tags, and tags can be duplicated for different
firmwares.
This would allow an OEM to say *this set of firmware has been tested as a set for workload A,
and this other set of firmware has been tested for workload B* which is fairly typical for
enterprise deployments.

## LVFS

The LVFS added support for “vendor-defined” component `<tag>`s in 2021, which are also supported in
fwupd since version 1.7.3.
These tags are typically used by OEMs to identify a manifest of firmware for a specific machine SKU.
This is opt-in for each vendor, and so if you are a vendor reading this and want to use this feature,
let us know by [opening an issue](https://gitlab.com/fwupd/lvfs-website/-/issues).

## Client

When provisioning the client machine, we can set the BKC by setting `HostBkc=vendor-2021q1` in
`/etc/fwupd/daemon.conf`.
Then any invocation of `fwupdmgr sync-bkc` will install or downgrade firmware on all compatible
devices (UEFI, RAID, network adapter, & SAS HBA etc.) to make the system match a compatible set.

Updating or downgrading firmware away from the *Best Known Configuration* is allowed, but the UI
shows a warning.
Using `fwupdmgr sync-bkc` will undo any manual changes and bring the machine back to the BKC.

## Local metadata

To define a locally defined BKC, extra metadata is read from the `/usr/share/fwupd/local.d` and
`/var/lib/fwupd/local.d` directories. For instance:

For example:

    <?xml version='1.0' encoding='utf-8'?>
    <components origin="mycompanyname">
      <component merge="append">
        <provides>
          <firmware>3ef35d3b-ceeb-5e27-8c0a-ac25f90367ab</firmware>
          <firmware>2ef35d3b-ceeb-5e27-8c0a-ac25f90367ac</firmware>
          <firmware>1ef35d3b-ceeb-5e27-8c0a-ac25f90367ad</firmware>
        </provides>
        <releases>
          <release version="225.53.1649"/>
          <release version="224.48.1605"/>
        </releases>
        <tags>
          <tag>mycompanyname-2022q1</tag>
        </tags>
      </component>
    </components>

This then appears when getting the releases for that specific GUID:

    fwupdmgr get-releases --json 3ef35d3b-ceeb-5e27-8c0a-ac25f90367ab
    {
      "Releases" : [
        {
          ...
          "Version" : "225.53.1649",
          "Tags" : [
            "mycompanyname-2022q1"
          ],
          ...
        },
        {
          ...
          "Version" : "224.48.1605",
          "Tags" : [
            "mycompanyname-2022q1"
          ],
          ...
        },
        {
          ...
          "Version" : "224.45.1389",
          ...
        }
      ]
    }

..and can be synced on the command line:

    $ fwupdmgr sync-bkc
    ╔══════════════════════════════════════════════════════════════════════════════╗
    ║ Downgrade System Firmware from 225.52.1521 to 225.53.1649?                   ║
    ╠══════════════════════════════════════════════════════════════════════════════╣
    ║ This release sets the number of HBAs supported by the system to 1024.        ║
    ╚══════════════════════════════════════════════════════════════════════════════╝
    Perform operation? [Y|n]:
