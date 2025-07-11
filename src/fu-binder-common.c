/*
 * Copyright 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBinderCommon"

#include "fu-binder-common.h"

#include <android/binder_process.h>
#include <android/binder_status.h>

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
