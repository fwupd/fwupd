package org.freedesktop.fwupd;

import org.freedesktop.fwupd.IFwupdEventListener;

interface IFwupd {
        PersistableBundle[] getDevices();
        void install(in String id, in ParcelFileDescriptor firmwareFd, in PersistableBundle options);
        void addEventListener(IFwupdEventListener listener);
        PersistableBundle[] getUpdates(in String id);
        PersistableBundle getProperties(in String[] property_names);
        PersistableBundle[] getRemotes();
        void updateMetadata(in String remoteId, in ParcelFileDescriptor dataFd, in ParcelFileDescriptor signatureFd);
}
