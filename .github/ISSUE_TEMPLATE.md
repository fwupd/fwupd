To help us pinpoint your issue, please insert the output of the
following commands when ran on the system with the issue:

```shell
$ fwupdmgr --version
**INSERT OUTPUT HERE**
```

Note, the switch `--version` is only present since version 0.9.6. If you use an
earlier version, please use the package manager to find out the package
version. For example, `dpkg -l fwupd`.

```shell
$ fwupdmgr get-devices
**INSERT OUTPUT HERE**
```

```shell
$ efibootmgr -v
**INSERT OUTPUT HERE**
**This is only required if you use the UEFI plugin**
```

```shell
$ efivar -l | grep fw
**INSERT OUTPUT HERE**
**This is only required if you use the UEFI plugin**
```

```shell
$ tree /boot
**INSERT OUTPUT HERE**
**This is only required if you use the UEFI plugin**
**We're looking for any `.cap` files and the location of `fwupx64.efi`**
```

Please answer the following questions:

- Operating system and version:
- How did you install fwupd (ex: `from source`, `pacman`, `apt-get`, etc):
- Have you tried rebooting?
- Are you using an NVMe disk?
- Is secure boot enabled (only for the UEFI plugin)?
