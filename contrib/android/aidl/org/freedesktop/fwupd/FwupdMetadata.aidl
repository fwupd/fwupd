package org.freedesktop.fwupd;

import android.os.ParcelFileDescriptor;

@VintfStability
parcelable FwupdMetadata {
    String remoteId;
    ParcelFileDescriptor dataFd;
    ParcelFileDescriptor signatureFd;
}
