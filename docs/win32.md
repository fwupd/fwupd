---
title: Windows Support
---

## Introduction

The Windows MSI package is built automatically in continuous integration.
Only some plugins are included, any plugins that use Linux-specific features like `udev`, `efivarfs` or `devfs` are disabled.
However, some USB devices updatable in Linux are also updatable in Microsoft Windows too.

## Installation

First, install the `.msi` package from the [release announcements](https://github.com/fwupd/fwupd/releases/), or any of the CI artifacts such as `dist/setup/fwupd-1.9.9-setup-x86_64.msi`.

Then click **More info** and **Run anyway** buttons:

![run anyway](win32-run-anyway.png)

If not already an administrator, click "yes" on the User Account Control (UAC) dialog:

![uac](win32-uac.png)

There are currently no start menu or desktop installed, although this is something we want to add in the future.
In the meantime, use the start menu to open a *Command Prompt*:

![start-menu](win32-start-menu.png)

Then click **yes** on the next UAC dialog if you chose to run the Command Prompt as the Administrator:

![uac again](win32-uac2.png)

Then navigate to `C:\Program Files (x86)\fwupd\bin`

![term](win32-term1.png)

Then `fwupdtool.exe` can be used just like `fwupdtool` on Linux or macOS.

![term again](win32-term2.png)

Note: the `fwupd.exe` process also works, and you can use another Command Prompt window to run `fwupdmgr.exe` from the same location.

There is no dbus-daemon on Windows, and so any local D-Bus client should also then connect to IP address 127.0.0.1 with TCP port 1341 rather than resolving the well-known `org.freedesktop.fwupd` bus name.
