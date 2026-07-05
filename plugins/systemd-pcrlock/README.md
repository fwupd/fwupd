---
title: Plugin: systemd-pcrlock
---

## Introduction

This plugin integrates with
[systemd-pcrlock](https://www.freedesktop.org/software/systemd/man/latest/systemd-pcrlock.html)
so that firmware and SecureBoot updates can be deployed without the user having
to type the volume recovery key on the next boot.

`systemd-pcrlock` predicts and reseals a TPM2 policy that protects secrets (such
as the disk encryption key) against the measurements recorded in the TPM PCRs. A
firmware update changes the firmware code and configuration measurements (PCRs
0, 2, 4 and 1, 3, 5), and a SecureBoot key database update changes the
SecureBoot measurements (PCR 7), which would otherwise invalidate the sealed
policy and require the recovery key after the update has been applied.

Before an update that affects the measured boot state is applied, this plugin
asks `systemd-pcrlock` to remove the `.pcrlock` files that pin the relevant
current measurements and to regenerate the policy. If this fails the update is
aborted, so that the firmware is never changed while the sealed policy still
requires the old measurements, which would otherwise leave the disk impossible
to unlock unattended. Once the machine has rebooted and applied the update, the
`systemd-pcrlock-*.service` units re-lock the policy against the new
measurements automatically.

The plugin is only active when `systemd-pcrlock` is being used to protect the
system, which is detected by the presence of a generated policy in
`/var/lib/systemd/pcrlock.json`.

## GUID Generation

This plugin does not create any devices and so does not generate any GUIDs.

## Vendor ID Security

This protocol does not create a device and thus requires no vendor ID set.

## External Interface Access

This plugin requires access to the `systemd-pcrlock` Varlink service at
`/run/systemd/io.systemd.PCRLock`, providing the `io.systemd.PCRLock.Lock`
method (added in systemd v262).

## Version Considerations

This plugin has been available since fwupd version `2.1.7`.
