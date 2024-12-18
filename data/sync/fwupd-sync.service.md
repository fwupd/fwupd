---
title: Best Known Configuration Synchronization
---

% fwupd-sync.service(5) {{PACKAGE_VERSION}} | Message Of The Day Integration

NAME
----

**fwupd-sync.service** — best known configuration synchronization.

DESCRIPTION
-----------

Devices with either a set branch or systemd with a configured BKC need to be installed with the
correct firmware version when they are inserted.

This optional service watches for devices being added to fwupd and attempts to automatically
install the correct firmware version on them.

To enable this, run:

  $ systemctl enable fwupd-sync.service
  $ systemctl start fwupd-sync.service

NOTES
-----

`fwupd-sync.service` also requires `fwupd-refresh.service` to ensure the metadata is up to date.

To enable a proxy, set the systemd global environment in `/etc/systemd/system.conf` so all the
systemd child processes have the proxy settings applied:

  [Manager]
  DefaultEnvironment=http_proxy=<http://internal.corp:3128/> https_proxy=<http://internal.corp:3128/>

SEE ALSO
--------

`fwupdmgr(1)`
`fwupd.conf(5)`
`fwupd-refresh.service(5)`
