package org.freedesktop.fwupd;

import android.os.ParcelFileDescriptor;

@VintfStability
parcelable FwupdInstallRequest {
    String id;
    ParcelFileDescriptor firmwareFd;
    long flags;
}
