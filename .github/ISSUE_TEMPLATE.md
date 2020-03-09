To help in root causing the issue, please insert the output of the
following commands from the system with the issue:

```shell
$ fwupdmgr --version
**INSERT OUTPUT HERE**
```

```shell
$ fwupdmgr get-devices
**INSERT OUTPUT HERE**
```

Please answer the following questions:

- Operating system and version:
- How did you install fwupd (examples: `apt`, `dnf`, `pacman`, `snap`, `source`):
- Have you tried rebooting?
- Is this a regression?

<details><summary>UEFI issues</summary>
<p>
Please fill theses out only for UEFI specific issues:

```shell
$ efibootmgr -v
**INSERT OUTPUT HERE**
```

```shell
$ efivar -l | grep fw
**INSERT OUTPUT HERE**
```

```shell
$ tree /boot
**INSERT OUTPUT HERE**
```
Additional questions:
- Are you using an NVMe disk?
- Is secure boot enabled (only for the UEFI plugin)?
</p>
</details>
