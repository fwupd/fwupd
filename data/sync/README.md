# Best Known Configuration Synchronization

Devices with either a set branch or systemd with a configured BKC need to be installed with the
correct firmware version when they are inserted.

This optional service watches for devices being added to fwupd and attempts to automatically
install the correct firmware version on them.

To enable this, run

```shell
# systemctl enable fwupd-sync.service
# systemctl start fwupd-sync.service
```
