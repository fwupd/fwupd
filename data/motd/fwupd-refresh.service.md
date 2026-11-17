---
title: Message Of The Day Integration
---

% fwupd-refresh.service(8) {{PACKAGE_VERSION}} | Message Of The Day Integration

## NAME

**fwupd-refresh.service** — message of the day integration.

## DESCRIPTION

Message on the day integration is used to display the availability of updates when connecting to
a remote console.

It has three elements:

* Automatic firmware metadata refresh

* Automatic firmware sync to the best known configuration (BKC)

* Message of the day display

The service only syncs firmware on hosts with a `HostBkc` set in fwupd.conf(5) or a quirk file,
or for devices with a firmware branch assigned.

Updates installed by the sync can change the TPM measurements that seal disk encryption keys.
Ensure volume recovery keys are available before enabling the timer on a host with a `HostBkc`
or device branch set.

This uses a systemd timer to run on a regular cadence. To enable this, run:

```shell
  systemctl enable fwupd-refresh.timer
```

## NOTES

The service has a timeout of 5 minutes (300 seconds) to prevent it from hanging indefinitely if
network conditions are poor or if the metadata download stalls.

Motd display is dependent upon the availability of the update-motd snippet consumption service
such as `pam_motd`.

To enable a proxy, set the systemd global environment in `/etc/systemd/system.conf` so all the
systemd child processes have the proxy settings applied:

```text
[Manager]
DefaultEnvironment=http_proxy="http://internal.corp:3128/ "https_proxy="http://internal.corp:3128/"
```

## SEE ALSO

<fwupdmgr(1)>
<fwupd.conf(5)>
