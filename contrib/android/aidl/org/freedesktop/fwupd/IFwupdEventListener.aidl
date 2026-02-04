package org.freedesktop.fwupd;

interface IFwupdEventListener {
    oneway void onChanged();
    oneway void onDeviceAdded(in PersistableBundle device);
    oneway void onDeviceRemoved(in PersistableBundle device);
    oneway void onDeviceChanged(in PersistableBundle device);
    oneway void onDeviceRequest(in PersistableBundle request);
    oneway void onPropertiesChanged(in PersistableBundle properties);
}