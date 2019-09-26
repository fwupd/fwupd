Adding a new plugin
-------------------

An extensible architecture allows for providing new plugin types (for reading
and writing different firmware) as well as ways quirk their behavior.

You can find more information about the architecture in the developers section
of the [fwupd website](https://fwupd.org).

You can use the [fwupd developer documentation](https://fwupd.github.io) to assist
with APIs available to write the plugin.

If you have a firmware specification and would like to see support
in this project, please file an issue and share the spec.  Patches are also
welcome.

We will not accept plugins that upgrade hardware using a proprietary Linux
executable, proprietary UEFI executable, proprietary library, or DBus interface.

Plugin interaction
------------------
Some plugins may be able to influence the behavior of other plugins.
This includes things like one plugin turning on a device, or providing missing
metadata to another plugin.

The ABI for these interactions is defined in:
https://github.com/fwupd/fwupd/blob/master/src/fu-device-metadata.h

All interactions between plugins should have the interface defined in that file.
