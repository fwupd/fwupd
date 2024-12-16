package org.freedesktop.fwupd;

interface IFwupdEventListener {
    void onChanged();
    void onDeviceAdded(in PersistableBundle device);
    void onDeviceRemoved(in PersistableBundle device);
    void onDeviceChanged(in PersistableBundle device);
    void onDeviceRequest(in PersistableBundle request);
}