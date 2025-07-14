/*
 * Copyright 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_status.h>
#include <android/persistable_bundle.h>
#include <glib/glib.h>

#define BINDER_DEFAULT_IFACE	    "org.freedesktop.fwupd.IFwupd"
#define BINDER_EVENT_LISTENER_IFACE "org.freedesktop.fwupd.IFwupdEventListener"
#define BINDER_SERVICE_NAME	    "fwupd"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AStatus, AStatus_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AParcel, AParcel_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(APersistableBundle, APersistableBundle_delete)

/* See contrib/android/aidl/org/freedesktop/fwupd
 * The order as defined in the IFwupd.aidl and IFwupdEventListener.aidl files defines the
 * transaction code.
 */

enum fu_binder_call {
	FWUPD_BINDER_CALL_GET_DEVICES = FIRST_CALL_TRANSACTION,
	FWUPD_BINDER_CALL_INSTALL,
	FWUPD_BINDER_CALL_ADD_EVENT_LISTENER,
	FWUPD_BINDER_CALL_GET_UPGRADES,
	FWUPD_BINDER_CALL_GET_PROPERTIES,
	FWUPD_BINDER_CALL_GET_REMOTES,
	FWUPD_BINDER_CALL_UPDATE_METADATA,
	/*< private >*/
	FWUPD_BINDER_CALL_LAST,
	FWUPD_BINDER_CALL_COUNT = FWUPD_BINDER_CALL_LAST - FIRST_CALL_TRANSACTION,
};

enum fu_binder_listener_call {
	FWUPD_BINDER_LISTENER_CALL_ON_CHANGED = FIRST_CALL_TRANSACTION,
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_ADDED,
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REMOVED,
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_CHANGED,
	FWUPD_BINDER_LISTENER_CALL_ON_DEVICE_REQUEST,
	FWUPD_BINDER_LISTENER_CALL_ON_PROPERTIES_CHANGED,
	/*< private >*/
	FWUPD_BINDER_LISTENER_CALL_LAST,
	FWUPD_BINDER_LISTENER_CALL_COUNT = FWUPD_BINDER_LISTENER_CALL_LAST - FIRST_CALL_TRANSACTION,
};

const gchar *
fu_binder_get_daemon_transaction_name(transaction_code_t code);
const gchar *
fu_binder_get_listener_transaction_name(transaction_code_t code);

GSource *
fu_binder_fd_source_new(gint fd);

binder_status_t
fu_binder_daemon_method_invocation_return_error(AParcel *out, GError *error);
binder_status_t
fu_binder_daemon_method_invocation_return_error_literal(AParcel *out,
							gint code,
							const gchar *message);
binder_status_t
fu_binder_daemon_method_invocation_return_variant(AParcel *out, GVariant *value, GError **error);
