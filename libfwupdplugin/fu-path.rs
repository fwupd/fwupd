// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Path types to use when dynamically determining a path at runtime.
#[derive(ToString)]
enum FuPathKind {
    // The cache directory (/var/cache/fwupd)
    CachedirPkg,
    // The non-volatile data store (/usr/share/fwupd)
    DatadirPkg,
    // The location to store EFI apps before install (/usr/libexec/fwupd/efi)
    Efiappdir,
    // The local state directory (/var)
    Localstatedir,
    // The local state directory for the package (/var/lib/fwupd)
    LocalstatedirPkg,
    // The location to look for plugins for package (/usr/lib/[triplet]/fwupd-plugins-3)
    LibdirPkg,
    // The configuration location (/etc)
    Sysconfdir,
    // The package configuration location (/etc/fwupd)
    SysconfdirPkg,
    // The sysfs base location (/sys)
    Sysfsdir,
    // The sysfs firmware location (/sys/firmware)
    SysfsdirFw,
    // The platform sysfs directory (/sys/bus/platform/drivers)
    SysfsdirDrivers,
    // The TPM sysfs directory (/sys/class/tpm)
    SysfsdirTpm,
    // The procfs location (/proc)
    Procfs,
    // The directory for polkit actions (/usr/share/polkit-1/actions/)
    PolkitActions,
    // The sysfs security location (/sys/kernel/security)
    SysfsdirSecurity,
    // The location of the ACPI tables
    AcpiTables,
    // The runtime directory (/run)
    Rundir,
    // The lock directory (/run/lock)
    Lockdir,
    // The firmware attributes directory (/sys/class/firmware-attributes)
    SysfsdirFwAttrib,
    // The path to configure the kernel policy for runtime loading other than /lib/firmware (/sys/module/firmware_class/parameters/path)
    FirmwareSearch,
    // The quirks data store (/usr/share/fwupd/quirks.d)
    DatadirQuirks,
    // The local state directory for quirks (/var/lib/fwupd/quirks.d)
    LocalstatedirQuirks,
    // The local state directory for metadata (/var/lib/fwupd/metadata)
    LocalstatedirMetadata,
    // The local state directory for remotes (/var/lib/fwupd/remotes.d)
    LocalstatedirRemotes,
    // The root of the install directory on Windows
    Win32Basedir,
    // The package configuration override (/var/etc/fwupd)
    LocalconfdirPkg,
    // The sysfs DMI location, (/sys/class/dmi/id)
    SysfsdirDmi,
    // The root of the host filesystem (/)
    HostfsRoot,
    // The host boot directory, (/boot)
    HostfsBoot,
    // The host dev directory, (/dev)
    Devfs,
    // The timezone symlink (/etc/localtime)
    Localtime,
    // The directory to launch executables
    Libexecdir,
    // The directory launch executables packaged with daemon
    LibexecdirPkg,
    // The vendor ID store (/usr/share/hwdata)
    DatadirVendorIds,
    // The debugfs directory (/sys/kernel/debug)
    Debugfsdir,
}
