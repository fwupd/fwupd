/*
 * Copyright 2024 Collabora Ltd. <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <android/binder_ibinder.h>

#define BINDER_DEFAULT_IFACE	    "org.freedesktop.fwupd.IFwupd"
#define BINDER_EVENT_LISTENER_IFACE "org.freedesktop.fwupd.IFwupdEventListener"
#define BINDER_SERVICE_NAME	    "fwupd"

enum fu_binder_calls {
	FWUPD_BINDER_CALL_GET_DEVICES =
	    FIRST_CALL_TRANSACTION, /* PersistableBundle[] getDevices(); */
	FWUPD_BINDER_CALL_INSTALL, /* void install(in String id, in ParcelFileDescriptor firmwareFd,
				      in @nullable PersistableBundle options); */
	FWUPD_BINDER_CALL_ADD_EVENT_LISTENER, /* void addEventListener(IFwupdEventListener
						 listener); */
};

enum fu_listener_binder_calls {
	FWUPD_BINDER_LISTENER_CALL_ON_CHANGED = FIRST_CALL_TRANSACTION, /* void onChanged(); */
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_ADDED,   /* void onDeviceAdded(in PersistableBundle
							 device); */
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REMOVED, /* void onDeviceRemoved(in PersistableBundle
							 device); */
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_CHANGED, /* void onDeviceChanged(in PersistableBundle
							 device); */
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REQUEST, /* void onDeviceRequest(in PersistableBundle
							 request); */
};
