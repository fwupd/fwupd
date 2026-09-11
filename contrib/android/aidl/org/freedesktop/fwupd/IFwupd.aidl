package org.freedesktop.fwupd;

import android.os.ParcelFileDescriptor;
import org.freedesktop.fwupd.FwupdDevice;
import org.freedesktop.fwupd.FwupdRelease;
import org.freedesktop.fwupd.FwupdInstallRequest;
import org.freedesktop.fwupd.FwupdMetadata;
import org.freedesktop.fwupd.IFwupdEventListener;
import org.freedesktop.fwupd.FwupdProperties;
import org.freedesktop.fwupd.FwupdRemote;
import org.freedesktop.fwupd.FwupdHwid;
import org.freedesktop.fwupd.FwupdPlugin;
import org.freedesktop.fwupd.FwupdKeyValue;
import org.freedesktop.fwupd.FwupdBiosSetting;
import org.freedesktop.fwupd.FwupdSecurityAttr;

@VintfStability
interface IFwupd {
        FwupdDevice[] getDevices();
        void install(in FwupdInstallRequest request);
        void addEventListener(IFwupdEventListener listener);
        FwupdRelease[] getUpdates(in String id);
        FwupdRelease[] getReleases(in String id);
        FwupdProperties getProperties();
        FwupdRemote[] getRemotes();
        void updateMetadata(in FwupdMetadata metadata);
        FwupdHwid[] getHwids();
        FwupdPlugin[] getPlugins();
        FwupdDevice[] getHistory();
        void setFeatureFlags(long flags);
        void activate(in String id);
        void unlock(in String id);
        void verify(in String id);
        void verifyUpdate(in String id);
        void modifyRemote(in String remoteId, in String key, in String value);
        void cleanRemote(in String remoteId);
        void modifyDevice(in String deviceId, in String key, in String value);
        void modifyConfig(in String section, in String key, in String value);
        void resetConfig(in String section);
        void clearResults(in String deviceId);
        FwupdDevice[] getDetails(in ParcelFileDescriptor fd);
        FwupdKeyValue[] getReportMetadata();
        FwupdBiosSetting[] getBiosSettings();
        void setBiosSettings(in FwupdKeyValue[] settings);
        FwupdSecurityAttr[] getHostSecurityAttrs();
        FwupdSecurityAttr[] getHostSecurityEvents(in int limit);
}
