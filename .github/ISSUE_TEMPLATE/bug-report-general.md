---
name: Bug report (General)
about: Create a report to help us improve
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
fwupdmgr --version
```

Please note how you installed it (`apt`, `dnf`, `pacman`, source, etc):

<details>

<summary>**fwupd device information**</summary>

Please provide the output of the fwupd devices recognized in your system.

```shell
fwupdmgr get-devices --show-all-devices
```

</details>

**Additional questions**

- Operating system and version:
- Have you tried rebooting?
- Is this a regression?
