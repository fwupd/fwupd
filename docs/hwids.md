---
title: Hardware IDs
---

## Introduction

Hardware IDs are used by fwupd to identify specific hardware.
This is useful as the device-specific identifiers may be the same for different firmware streams.
Each hardware ID has varying levels of specificity for the hardware, for instance matching only the
system OEM, or matching up to 8 fields including the system BIOS version.

For instance, Dell and Lenovo might ship a wireless broadband modem with the same chip vendor and
product IDs of `USB\VID_0BDA&PID_5850` and although the two OEMs share the same internal device,
the firmware may be different.
To cover this case fwupd allows adding `hardware` requirements that mean we can deploy firmware that
targets `USB\VID_0BDA&PID_5850`, but *only for Dell* or *only for Lenovo* systems.

Microsoft calls these values "CHIDs" and they are generated on Windows from the SBMIOS tables using `ComputerHardwareIds.exe`
binary which can be found [here](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/computerhardwareids).
The list of CHIDs used in Microsoft Windows is:

    HardwareID-0  ← Manufacturer + Family + Product Name + SKU Number + BIOS Vendor + BIOS Version + BIOS Major Release + BIOS Minor Release
    HardwareID-1  ← Manufacturer + Family + Product Name + BIOS Vendor + BIOS Version + BIOS Major Release + BIOS Minor Release
    HardwareID-2  ← Manufacturer + Product Name + BIOS Vendor + BIOS Version + BIOS Major Release + BIOS Minor Release
    HardwareID-3  ← Manufacturer + Family + ProductName + SKU Number + Baseboard_Manufacturer + Baseboard_Product
    HardwareID-4  ← Manufacturer + Family + ProductName + SKU Number
    HardwareID-5  ← Manufacturer + Family + ProductName
    HardwareID-6  ← Manufacturer + SKU Number + Baseboard_Manufacturer + Baseboard_Product
    HardwareID-7  ← Manufacturer + SKU Number
    HardwareID-8  ← Manufacturer + ProductName + Baseboard_Manufacturer + Baseboard_Product
    HardwareID-9  ← Manufacturer + ProductName
    HardwareID-10 ← Manufacturer + Family + Baseboard_Manufacturer + Baseboard_Product
    HardwareID-11 ← Manufacturer + Family
    HardwareID-12 ← Manufacturer + Enclosure Type
    HardwareID-13 ← Manufacturer + Baseboard_Manufacturer + Baseboard_Product
    HardwareID-14 ← Manufacturer

On Windows, CHIDs are generated from the ASCII representation of SMBIOS strings, and on Linux the same
mechanism is used. Additionally, on Linux, the Device Tree, DMI and `kenv` data sources
are used to construct emulations of the Microsoft CHIDs.

When installing firmware and drivers in Windows vendors *already use* the generated HardwareID GUIDs
that match SMBIOS keys like the BIOS vendor and the product SKU.

Both `fwupdtool hwids` and `ComputerHardwareIds.exe` only compute results that have the necessary
data values available.
If a data field is missing, then any related CHIDs are not generated.
For example, if the SKU field is missing, then `HardwareID` 0, 3, 4 6 and 7 will not be available for
that particular system.

## Implementation

Users with versions of fwupd newer than 1.1.1 can run `sudo fwupdtool hwids`. For example:

    Computer Information
    --------------------
    BiosVendor: LENOVO
    BiosVersion: GJET75WW (2.25 )
    Manufacturer: LENOVO
    Family: ThinkPad T440s
    ProductName: 20ARS19C0C
    ProductSku: LENOVO_MT_20AR_BU_Think_FM_ThinkPad T440s
    EnclosureKind: 10
    BaseboardManufacturer: LENOVO
    BaseboardProduct: 20ARS19C0C

    Hardware IDs
    ------------
    {c4159f74-3d2c-526f-b6d1-fe24a2fbc881}   <- Manufacturer + Family + ProductName + ProductSku + BiosVendor + BiosVersion + BiosMajorRelease + BiosMinorRelease
    {ff66cb74-5f5d-5669-875a-8a8f97be22c1}   <- Manufacturer + Family + ProductName + BiosVendor + BiosVersion + BiosMajorRelease + BiosMinorRelease
    {2e4dad4e-27a0-5de0-8e92-f395fc3fa5ba}   <- Manufacturer + ProductName + BiosVendor + BiosVersion + BiosMajorRelease + BiosMinorRelease
    {3faec92a-3ae3-5744-be88-495e90a7d541}   <- Manufacturer + Family + ProductName + ProductSku + BaseboardManufacturer + BaseboardProduct
    {660ccba8-1b78-5a33-80e6-9fb8354ee873}   <- Manufacturer + Family + ProductName + ProductSku
    {8dc9b7c5-f5d5-5850-9ab3-bd6f0549d814}   <- Manufacturer + Family + ProductName
    {178cd22d-ad9f-562d-ae0a-34009822cdbe}   <- Manufacturer + ProductSku + BaseboardManufacturer + BaseboardProduct
    {da1da9b6-62f5-5f22-8aaa-14db7eeda2a4}   <- Manufacturer + ProductSku
    {059eb22d-6dc7-59af-abd3-94bbe017f67c}   <- Manufacturer + ProductName + BaseboardManufacturer + BaseboardProduct
    {0cf8618d-9eff-537c-9f35-46861406eb9c}   <- Manufacturer + ProductName
    {f4275c1f-6130-5191-845c-3426247eb6a1}   <- Manufacturer + Family + BaseboardManufacturer + BaseboardProduct
    {db73af4c-4612-50f7-b8a7-787cf4871847}   <- Manufacturer + Family
    {5e820764-888e-529d-a6f9-dfd12bacb160}   <- Manufacturer + EnclosureKind
    {f8e1de5f-b68c-5f52-9d1a-f1ba52f1f773}   <- Manufacturer + BaseboardManufacturer + BaseboardProduct
    {6de5d951-d755-576b-bd09-c5cf66b27234}   <- Manufacturer

Which matches the output of `ComputerHardwareIds.exe` on the same hardware.
