---
title: fwupd terminal user interface
---

% fwupdtui(1) {{PACKAGE_VERSION}} | fwupd terminal user interface man page

## NAME

**fwupdtui** — view and modify firmware settings

## SYNOPSIS

| **fwupdtui** [**\-\-version**]

## DESCRIPTION

**fwupdtui** is an interactive terminal interface for firmware settings exposed by fwupd.
It uses libfwupd through GObject Introspection and requires the fwupd daemon to be running.

Settings are shown in a searchable hierarchy. Select a writable setting and press **e** or
**Enter** to change it. Changes are collected locally until **Ctrl+S** or the **Apply** button is
used. Press **q** or use the **Quit** button to discard unapplied changes.

Keyboard and mouse input are supported by the Textual interface.

## OPTIONS

**\-\-version**
: Show the fwupd version and exit.

**\-h**, **\-\-help**
: Show help and exit.

## EXIT STATUS

Successful operation returns "0". A failure to load or modify firmware settings returns "1".

## BUGS

See GitHub Issues: <https://github.com/fwupd/fwupd/issues>

## SEE ALSO

<fwupdmgr(1)>
<fwupd(8)>
