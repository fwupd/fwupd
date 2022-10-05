---
name: Bug report (Dell WD19)
about: Create a report to help us improve
title: 'Dell WD19 upgrade issue'
labels: bug
assignees: 'cragw'

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
fwupdmgr --version
```

Please note how you installed it (`apt`, `dnf`, `pacman`, source, etc):

<details>

<summary>**fwupd device information**</summary>

Please provide the output of the external fwupd devices recognized in your system.

```shell
fwupdmgr get-devices --filter=~internal
```

</details>

**Dock SKU**
Please mention which module is installed in your WD19.

- [ ] WD19 (Single-C)
- [ ] WD19TB (Thunderbolt)
- [ ] WD19DC (Dual-C)

**Peripherals connected to the dock**
Please describe all devices connected to the dock.  Be as specific as possible,
including USB devices, hubs, monitors, and downstream type-C devices.

**Verbose daemon logs**
First enable daemon verbose logs collection.

```shell
fwupdmgr modify-config "VerboseDomains" "*"
```

Then try to reproduce the issue.  Even if it doesn't reproduce, please attach the
daemon verbose logs collected from the system journal.

```shell
journalctl -b -u fwupd.service
```

**Additional questions**

- Operating system and version:
- Have you tried unplugging the dock or any peripherals from your machine?
- Have you tried to power cycle the dock from the AC adapter?
- Is this a regression?
