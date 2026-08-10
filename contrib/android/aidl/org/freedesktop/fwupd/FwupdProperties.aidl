package org.freedesktop.fwupd;

@VintfStability
parcelable FwupdProperties {
    @nullable String daemonVersion;
    @nullable String hostBkc;
    @nullable String hostVendor;
    @nullable String hostProduct;
    @nullable String hostMachineId;
    @nullable String hostSecurityId;
    boolean tainted;
    boolean interactive;
    boolean onlyTrusted;
    int status;
    int percentage;
    int batteryLevel;
    int batteryThreshold;
}