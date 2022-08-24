# BIOS Settings

On supported machines fwupd can enforce BIOS settings policy so that a user's desired settings are configured at bootup
and prevent fwupd clients from changing them.

## JSON policies

A policy file can be created using `fwupdmgr`.  First determine what settings you want to enforce by running:

```shell
# fwupdmgr get-bios-settings
```

After you have identified settings, create a JSON payload by listing them on the command line.  Any number of attributes can
be listed.
For example for the BIOS setting `WindowsUEFIFirmwareUpdate` you would create a policy file like this:

```shell
# fwupdmgr get-bios-settings --json WindowsUEFIFirmwareUpdate > ~/foo.json
```

Now examine `~/foo.json` and modify the `BiosSettingCurrentValue` key to your desired value.

Lastly place this policy file into `/etc/fwupd/bios-settings.d`.  Any number of policies is supported, and they will be examined
in alphabetical order.  The next time that fwupd is started it will load this policy and ensure that no fwupd clients change it.
