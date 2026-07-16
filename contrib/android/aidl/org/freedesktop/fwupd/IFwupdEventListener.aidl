package org.freedesktop.fwupd;

import org.freedesktop.fwupd.FwupdRequest;
import org.freedesktop.fwupd.FwupdProperties;
import org.freedesktop.fwupd.FwupdDevice;

@VintfStability
interface IFwupdEventListener {
    oneway void onChanged();
    oneway void onDeviceAdded(in FwupdDevice device);
    oneway void onDeviceRemoved(in FwupdDevice device);
    oneway void onDeviceChanged(in FwupdDevice device);
    oneway void onDeviceRequest(in FwupdRequest request);
    oneway void onPropertiesChanged(in FwupdProperties properties);
}