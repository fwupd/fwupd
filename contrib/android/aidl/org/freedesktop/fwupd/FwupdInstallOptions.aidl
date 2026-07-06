package org.freedesktop.fwupd;

@VintfStability
parcelable FwupdInstallOptions {
    boolean force;
    boolean allowOlder;
    boolean allowReinstall;
    boolean allowBranchSwitch;
}