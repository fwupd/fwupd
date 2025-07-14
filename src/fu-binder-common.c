/*
 * Copyright 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBinderCommon"

#include "fu-binder-common.h"

#include <android/binder_process.h>
#include <android/binder_status.h>

#include "fu-common.h"
#include "gparcelable.h"

const char *const fu_binder_call_names[FWUPD_BINDER_CALL_COUNT] = {
    "getDevices",
    "install",
    "addEventListener",
    "getUpgrades",
    "getProperties",
    "getRemotes",
    "updateMetadata",
};

const char *const fu_binder_listener_call_names[FWUPD_BINDER_LISTENER_CALL_COUNT] = {
    "onChanged",
    "onDeviceAdded",
    "onDeviceRemoved",
    "onDeviceChanged",
    "onDeviceRequest",
    "onPropertiesChanged",
};

const gchar *
fu_binder_get_daemon_transaction_name(transaction_code_t code)
{
	if (code < FIRST_CALL_TRANSACTION || code >= FWUPD_BINDER_CALL_LAST)
		return NULL;
	return fu_binder_call_names[code - 1];
}

const gchar *
fu_binder_get_listener_transaction_name(transaction_code_t code)
{
	if (code < FIRST_CALL_TRANSACTION || code >= FWUPD_BINDER_LISTENER_CALL_LAST)
		return NULL;
	return fu_binder_listener_call_names[code - 1];
}

typedef struct _FuBinderFdSource {
	GSource source;
	gpointer fd_tag;
} FuBinderFdSource;

static gboolean
binder_fd_source_check(GSource *source)
{
	FuBinderFdSource *binder_fd_source = (FuBinderFdSource *)source;
	return g_source_query_unix_fd(source, binder_fd_source->fd_tag) & G_IO_IN;
}

static gboolean
binder_fd_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
	binder_status_t nstatus = ABinderProcess_handlePolledCommands();

	if (nstatus != STATUS_OK) {
		AStatus *status = AStatus_fromStatus(nstatus);
		g_warning("failed to handle polled commands %s", AStatus_getDescription(status));
		// TODO: Should we stop polling?
		// return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

static GSourceFuncs binder_fd_source_funcs = {
    NULL,
    binder_fd_source_check,
    binder_fd_source_dispatch,
};

GSource *
fu_binder_fd_source_new(gint fd)
{
	GSource *source = g_source_new(&binder_fd_source_funcs, sizeof(FuBinderFdSource));
	FuBinderFdSource *binder_fd_source = (FuBinderFdSource *)source;
	binder_fd_source->fd_tag = g_source_add_unix_fd(source, fd, G_IO_IN | G_IO_ERR);
	return (GSource *)source;
}

binder_status_t
fu_binder_daemon_method_invocation_return_error(AParcel *out, GError *error)
{
	g_autoptr(AStatus) status = NULL;

	fu_error_convert(&error);

	g_return_val_if_fail(error != NULL, STATUS_BAD_VALUE);

	status = AStatus_fromServiceSpecificErrorWithMessage(error->code, error->message);

	return AParcel_writeStatusHeader(out, status);
}

binder_status_t
fu_binder_daemon_method_invocation_return_error_literal(AParcel *out,
							gint code,
							const gchar *message)
{
	g_autoptr(AStatus) status = NULL;

	g_return_val_if_fail(out != NULL, STATUS_UNEXPECTED_NULL);
	g_return_val_if_fail(message != NULL, STATUS_UNEXPECTED_NULL);

	status = AStatus_fromServiceSpecificErrorWithMessage(code, message);

	return AParcel_writeStatusHeader(out, status);
}

binder_status_t
fu_binder_daemon_method_invocation_return_variant(AParcel *out, GVariant *value, GError **error)
{
	gint out_start = AParcel_getDataPosition(out);

	AParcel_writeStatusHeader(out, AStatus_newOk());

	// TODO: Improve error checking
	if (value) {
		if (gp_parcel_write_variant(out, value, error) != STATUS_OK) {
			AParcel_setDataPosition(out, out_start);
			if (*error) {
				return fu_binder_daemon_method_invocation_return_error(out, *error);
			} else {
				return fu_binder_daemon_method_invocation_return_error_literal(
				    out,
				    FWUPD_ERROR_INTERNAL,
				    "failed to encode parcel, no error");
			}
		}
	}

	return STATUS_OK;
}
