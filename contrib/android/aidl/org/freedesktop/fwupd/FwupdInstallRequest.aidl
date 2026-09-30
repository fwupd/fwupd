package org.freedesktop.fwupd;

import android.os.ParcelFileDescriptor;

@VintfStability
parcelable FwupdInstallRequest {
    String id;
    @nullable ParcelFileDescriptor firmwareFd;
    long flags;
    @nullable String filenameHint;
}
