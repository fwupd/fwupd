---
name: Bug report (UEFI Updates)
about: Issues involving UEFI device updates
title: ''
labels: bug
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.


**Steps to Reproduce**
Steps to reproduce the behavior.


**Expected behavior**
A clear and concise description of what you expected to happen.

**fwupd version information**
Please provide the version of the daemon and client.
```shell
$ fwupdmgr --version
```

Please note how you installed it (`apt`, `dnf`, `pacman`, source, etc):

**fwupd device information**
Please provide the output of the fwupd devices recognized in your system.

```shell
$ fwupdmgr get-devices --show-all-devices
```

**System UEFI configuration**
Please provide the output of the following commands:
```shell
$ efibootmgr -v
```

```shell
$ efivar -l | grep fw
```

```shell
$ tree /boot
```

**Additional questions**
- Operating system and version:
- Have you tried rebooting?
- Is this a regression?
- Are you using an NVMe disk?
- Is secure boot enabled?
- Is this a Lenovo system with 'Boot Order Lock' turned on in the BIOS?
