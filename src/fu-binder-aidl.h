/*
 * Copyright 2024 Collabora Ltd. <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <android/binder_ibinder.h>

#define BINDER_DEFAULT_IFACE	    "org.freedesktop.fwupd.IFwupd"
#define BINDER_EVENT_LISTENER_IFACE "org.freedesktop.fwupd.IFwupdEventListener"
#define BINDER_SERVICE_NAME	    "fwupd"

/* See contrib/android/aidl/org/freedesktop/fwupd
 * The order as defined in the IFwupd.aidl and IFwupdEventListener.aidl files defines the
 * transaction code.
 */

enum fu_binder_calls {
	FWUPD_BINDER_CALL_GET_DEVICES = FIRST_CALL_TRANSACTION,
	FWUPD_BINDER_CALL_INSTALL,
	FWUPD_BINDER_CALL_ADD_EVENT_LISTENER,
	FWUPD_BINDER_CALL_GET_UPGRADES,
	FWUPD_BINDER_CALL_GET_PROPERTIES,
	FWUPD_BINDER_CALL_GET_REMOTES,
	FWUPD_BINDER_CALL_UPDATE_METADATA,
};

enum fu_listener_binder_calls {
	FWUPD_BINDER_LISTENER_CALL_ON_CHANGED = FIRST_CALL_TRANSACTION,
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_ADDED,
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REMOVED,
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_CHANGED,
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REQUEST,
	FWUPD_BINDER_LISTENER_CALL_ON_PROPERTIES_CHANGED,
};
