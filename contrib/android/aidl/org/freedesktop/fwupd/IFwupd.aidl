package org.freedesktop.fwupd;

import org.freedesktop.fwupd.FwupdDevice;
import org.freedesktop.fwupd.FwupdUpdate;
import org.freedesktop.fwupd.FwupdInstallRequest;
import org.freedesktop.fwupd.FwupdMetadata;
import org.freedesktop.fwupd.IFwupdEventListener;
import org.freedesktop.fwupd.FwupdProperties;
import org.freedesktop.fwupd.FwupdRemote;
import org.freedesktop.fwupd.FwupdHwid;

@VintfStability
interface IFwupd {
        FwupdDevice[] getDevices();
        void install(in FwupdInstallRequest request);
        void addEventListener(IFwupdEventListener listener);
        FwupdUpdate[] getUpdates(in String id);
        FwupdProperties getProperties(in String[] property_names);
        FwupdRemote[] getRemotes();
        void updateMetadata(in FwupdMetadata metadata);
        FwupdHwid[] getHwids();
}