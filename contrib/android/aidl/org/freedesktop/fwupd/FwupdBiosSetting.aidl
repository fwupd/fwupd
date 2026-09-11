package org.freedesktop.fwupd;

@VintfStability
parcelable FwupdBiosSetting {
    @nullable String id;
    @nullable String name;
    @nullable String description;
    @nullable String path;
    @nullable String currentValue;
    @nullable String[] possibleValues;
    int kind;
    boolean readOnly;
    long lowerBound;
    long upperBound;
    long scalarIncrement;
}
