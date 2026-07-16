package org.freedesktop.fwupd;

import android.os.ParcelFileDescriptor;
import org.freedesktop.fwupd.FwupdInstallOptions;

@VintfStability
parcelable FwupdInstallRequest {
    String id;
    ParcelFileDescriptor firmwareFd;
    FwupdInstallOptions options;
}
