package org.freedesktop.fwupd;

import org.freedesktop.fwupd.IFwupdEventListener;

interface IFwupd {
        PersistableBundle[] getDevices();
        void install(in String id, in ParcelFileDescriptor firmwareFd, in @nullable PersistableBundle options);
        //void install(in ParcelFileDescriptor firmwareFd);
        void addEventListener(IFwupdEventListener listener);
        // TODO: getProperty??? fwupd dbus exposes a few read only properties, they expose 6 strings and 3 booleans, 4 unsigned int
        //   dbus allows a client to listen for property changes, this could be added to the event listener as propertyChanged(String property)
        //   We could just provide a PersistableBundle containing all properties?
        //   Or we could split properties into groups, percentage maybe is an event, [daemon version, host vendor/product/machine_id] don't change frequently
        //     * Daemon info {version, tainted, interactive}
        PersistableBundle[] getUpdates(in String id);
}