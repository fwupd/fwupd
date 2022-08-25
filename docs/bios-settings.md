---
title: BIOS Settings API
---

fwupd 1.8.4 and later include the ability to modify BIOS settings on systems that support the Linux kernel's [firmware-attributes API](https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-class-firmware-attributes).

Drivers included with the Linux kernel on supported machines will advertise all BIOS settings that the OS can change.  The fwupd daemon uses this API create an abstraction that fwupd clients can use to offer BIOS settings to change to end users.

## Interactive command line usage

Both `fwupdmgr` and `fwupdtool` have support for the fwupd BIOS settings API both for interactive use.
Using `fwupdgmr` will require PolicyKit authentication as only authorized users can view the current BIOS settings.
Using `fwupdtool` will require running the tool as root, as `fwupdtool` only works as root.

### Getting available BIOS settings

To fetch available BIOS settings for a machine:

```shell
# fwupdmgr get-bios-setting
```

This will provide a listing of all available settings.  If you would like to just see a subset of attributes, you can list them space delimited on the command line.  For example:

```shell
$ fwupdmgr get-bios-setting WindowsUEFIFirmwareUpdate MmioAbove4GLimit 
Authenticating…          [ -                                     ]
WindowsUEFIFirmwareUpdate:
  Setting type:         Enumeration
  Current Value:        Enable
  Description:          BIOS updates delivered via LVFS or Windows Update
  Read Only:            False
  Possible Values:
    0:                  Disable
    1:                  Enable


MmioAbove4GLimit:
  Setting type:         Enumeration
  Current Value:        Auto
  Description:          MmioAbove4GLimit
  Read Only:            False
  Possible Values:
    0:                  Auto
    1:                  40
    2:                  42
    3:                  44
    4:                  46
    5:                  48
```

When using BASH as your shell, bash-completion can be used to discover BIOS settings. In the above example, those settings could be found from this series of tab actions:

```shell
$ sudo fwupdmgr get-bios-setting W
WakeonLAN                  WakeUponAlarm              WindowsUEFIFirmwareUpdate  
130 $ sudo fwupdmgr get-bios-setting WindowsUEFIFirmwareUpdate 
Display all 131 possibilities? (y or n)
AbsolutePersistenceModule     CStateSupport                 M2Slot2Port                   PCIeSlot3Bifurcation          PXEIPV6NetworkStack           SetStrongPassword
AccessSecuritySettings        DASHSupport                   MaxPasswordAttempts           PCIeSlot3DLFSupport           QuadM2PCIeCardFanControl      SmartUSBProtection
AfterPowerLoss                DataScrambling                MCRUSBHeader                  PCIeSlot3LinkSpeed            RealtimeDIAG                  SMT
AlarmDate(MM\DD\YYYY)         DeviceGuard                   MediaCardReader               PCIeSlot3Port                 RearAudioController           SRIOVSupport
AlarmDayofWeek                DevicePowerupDelay            MmioAbove4GLimit              PCIeSlot4Bifurcation          RearUSBPorts                  StartupSequence
AlarmTime(HH:MM:SS)           DeviceResetTimeout            --no-authenticate             PCIeSlot4DLFSupport           RemoteSetSMP                  USBChargingPortInS4S5
AllowJumperClearSVP           DIAG7SegMode                  NUMA                          PCIeSlot4LinkSpeed            RequireHDPonSystemBoot        USBPortAccess
AMDMemoryGuard                EnhancedPowerSavingMode       NVMeRAIDMode                  PCIeSlot4Port                 RequireSVPwhenFlashing        USBTransferTimeout
AMDSecureVirtualMachine       ErrorBootSequence             OnboardEthernetController     PCIeSlot5Bifurcation          SATAController                UserDefinedAlarmFriday
ASPMSupport                   FanControlStepping            OptionKeysDisplay             PCIeSlot5DLFSupport           SATADrive1                    UserDefinedAlarmMonday
AutomaticBootSequence         FrontUSBPorts                 PasswordChangeTime            PCIeSlot5LinkSpeed            SATADrive2                    UserDefinedAlarmSaturday
BIOSPasswordAtBootDeviceList  HardDiskPre-delay             PasswordCountExceededError    PCIeSlot5Port                 SATADrive3                    UserDefinedAlarmSunday
BIOSPasswordAtReboot          InternalUSB2Port              PatroScrub                    PCIeSlot6Bifurcation          SATADrive4                    UserDefinedAlarmThursday
BIOSPasswordAtSystemBoot      InternalUSB3Port              PatroScrubInterval            PCIeSlot6DLFSupport           SATADrive5                    UserDefinedAlarmTime
BootUpNumLockStatus           IOMMU                         PCIeSlot1Bifurcation          PCIeSlot6LinkSpeed            SATADrive6                    UserDefinedAlarmTuesday
CardLocation                  --json                        PCIeSlot1DLFSupport           PCIeSlot6Port                 SATADrive6HotPlugSupport      UserDefinedAlarmWednesday
ClearDIAGLog                  KeyboardLayout                PCIeSlot1LinkSpeed            pending_reboot                SecureBoot                    --verbose
ConfigurationChangeDetection  M2Slot1DLFSupport             PCIeSlot1Port                 PhysicalPresenceforClear      SecureRollBackPrevention      WakeonLAN
ConfigureSATAas               M2Slot1LinkSpeed              PCIeSlot2Bifurcation          POPChangeablebyUser           SecurityChip                  WakeUponAlarm
CoverTamperDetected           M2Slot1Port                   PCIeSlot2DLFSupport           PostPackageRepair             SelectActiveVideo             WindowsUEFIFirmwareUpdate
CPBMode                       M2Slot2DLFSupport             PCIeSlot2LinkSpeed            PrimaryBootSequence           SerialPort1Address            XHCIHandoff
CPUC6Report                   M2Slot2LinkSpeed              PCIeSlot2Port                 PXEIPV4NetworkStack           SetMinimumLength              
130 $ fwupdmgr get-bios-setting WindowsUEFIFirmwareUpdate MmioAbove4GLimit 
```

### Setting BIOS settings

To set one or more BIOS settings for a machine:

```shell
# fwupdmgr set-bios-setting SETTING VALUE
```

For supported attributes, `fwupdmgr` will also use tab completion in BASH for filling out a value.
For example to enable an enumeration attribute:

```shell
$ sudo fwupdmgr set-bios-setting W
WakeonLAN                  WakeUponAlarm              WindowsUEFIFirmwareUpdate  
130 $ sudo fwupdmgr set-bios-setting WindowsUEFIFirmwareUpdate 
Disable  Enable   
130 $ sudo fwupdmgr set-bios-setting WindowsUEFIFirmwareUpdate Enable
```

After setting an attribute you may be prompted to reboot as most settings will require a reboot to take effect.

```shell
$ fwupdmgr set-bios-setting WakeonLAN Primary 
Authenticating…          [   -                                   ]
Set BIOS setting 'WakeonLAN' using 'Primary'.

An update requires a reboot to complete. Restart now? [y|N]: 
```

If you would like to program multiple attributes, list them in pairs of the name of the attribute followed by the desired value.

## Programmatic command line usage

`fwupdmgr` offers support for the fwupd BIOS settings API programmatically as well for use in scripts. To use the programmatic API you will add the `--json` argument to your calls. The output or input will then be expected to be JSON payloads.

### Fetching BIOS settings programmatically

Below is an example of fetching the `WakeonLAN` setting.

```shell
# fwupdmgr get-bios-setting WakeonLAN --json
{
  "BiosSettings" : [
    {
      "Name" : "WakeonLAN",
      "Description" : "WakeonLAN",
      "Filename" : "/sys/class/firmware-attributes/thinklmi/attributes/WakeonLAN",
      "BiosSettingId" : "com.thinklmi.WakeonLAN",
      "BiosSettingCurrentValue" : "Primary",
      "BiosSettingReadOnly" : "false",
      "BiosSettingType" : 1,
      "BiosSettingPossibleValues" : [
        "Primary",
        "Automatic",
        "Disable"
      ]
    }
  ]
}
```

Similar to the interactive usage, if no parameters are provided all settings are fetched, and if desired multiple settings can be listed on the command line.

Error messages won't be emitted, you'll have to rely on the return code to tell if this was successful.
*NOTE:* To debug errors, use the `--verbose` argument to see messages related to the error.

### Setting BIOS settings programmatically

To set BIOS settings, a JSON payload will need to be crafted in advance.  The path to this payload is used as an argument.

```shell
# fwupdmgr set-bios-setting ~/foo.json --json
```

An important return code to know for programmatic usage is that *2* means nothing was done because all settings are already programmed. This message will also be emitted.
*NOTE:* To debug errors, use the `--verbose` argument to see messages related to the error.

## Firmware setting policies

`fwupd` has the ability to enforce the BIOS settings policy of a system administrator.  To use this feature, create a json payload using `fwupdmgr get-bios-setting --json` that reflects the settings you would like to see enforced.

Then copy this payload into `/etc/fwupd/bios-settings.d` with a filename ending in `.json`.  The next time that the fwupd daemon is started (such as a system bootup) it will ensure that all BIOS settings are programed to your desired values.  It will also mark those settings as read-only so no fwupd clients will be able to modify them.

This *does not* stop the kernel firmware-attributes API from working.  So a determined user with appropriate permissions would be able to modify settings from the kernel API directly, but they would be changed again on fwupd daemon startup.

## Settings types

The Linux kernel will offer the following types of BIOS settings:

* Enumeration: The setting will only accept a limiited list of possible values
* Integer: The setting will will accept a limited range of integer values
* String: The setting will accept a limited length UTF-8 string

All of those setting types are accepted by fwupd. It is expected that drivers or firmware will validate the input, but where possible fwupd will do validation of the input to give better error messages and avoid failures.

fwupd will also do a mapping where it can accept multiple cases or synonyms for words that are obviously positive or negative.  So for example if the setting expects `Enabled` but the user passes `tRUE` fwupd will map this into `Enabled` before sending it to the driver and the driver sending it to the firmware.

## libfwupd

`fwupdmgr` internally uses `FwupdClient` to manage BIOS settings.  Any other clients that are interested in managing BIOS settings can use this library as well. A sample python application is included in the `contrib/` directory in the fwupd source tree.
