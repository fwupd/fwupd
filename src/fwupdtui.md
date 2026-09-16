---
title: fwupd terminal user interface
---

% fwupdtui(1) {{PACKAGE_VERSION}} | fwupd terminal user interface man page

## NAME

**fwupdtui** — manage firmware and platform security

## SYNOPSIS

| **fwupdtui** [**\-\-version**]

## DESCRIPTION

**fwupdtui** is an interactive terminal interface for fwupd. It uses libfwupd through GObject
Introspection and requires the fwupd daemon to be running. The home screen provides three views:

**Manage BIOS Settings**
: Browse, search, and modify firmware settings. Changes are collected locally until **Ctrl+S** or
  the **Apply** button is used.

**Manage Devices**
: Inspect firmware devices and available releases; install upgrades, downgrades, reinstalls, or a
  local cabinet archive; refresh metadata; enable LVFS; unlock and activate devices; and verify or
  update firmware checksums.

**View Security Measurement**
: Inspect the Host Security ID, its grouped security attributes and runtime suffix, recent security
  events, and known device security issues.

Press **Esc** to return to the home screen and **q** to quit. BIOS settings and devices have
searchable hierarchies. Select a writable BIOS setting and press **e** or **Enter** to change it.

Keyboard and mouse input are supported by the Textual interface.

## OPTIONS

**\-\-version**
: Show the fwupd version and exit.

**\-h**, **\-\-help**
: Show help and exit.

## EXIT STATUS

Successful operation returns "0". A failure to connect to fwupd returns "1".

## BUGS

See GitHub Issues: <https://github.com/fwupd/fwupd/issues>

## SEE ALSO

<fwupdmgr(1)>
<fwupd(8)>
