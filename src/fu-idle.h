/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_IDLE (fu_idle_get_type())
G_DECLARE_FINAL_TYPE(FuIdle, fu_idle, FU, IDLE, GObject)

typedef enum {
	FU_IDLE_STATUS_UNKNOWN,
	FU_IDLE_STATUS_IDLE,
	FU_IDLE_STATUS_BUSY,
	FU_IDLE_STATUS_TIMEOUT,
	FU_IDLE_STATUS_LAST,
} FuIdleStatus;

FuIdle *
fu_idle_new(void);
guint32
fu_idle_inhibit(FuIdle *self, const gchar *reason);
void
fu_idle_uninhibit(FuIdle *self, guint32 token);
gboolean
fu_idle_has_inhibit(FuIdle *self, const gchar *reason);
void
fu_idle_set_timeout(FuIdle *self, guint timeout);
void
fu_idle_reset(FuIdle *self);
FuIdleStatus
fu_idle_get_status(FuIdle *self);

/**
 * FuIdleLocker:
 * @idle:	A #FuIdle
 * @token:	A #guint32 number
 *
 * A locker to prevent daemon from shutting down on its own
 **/
typedef struct {
	FuIdle *idle;
	guint32 token;
} FuIdleLocker;

FuIdleLocker *
fu_idle_locker_new(FuIdle *self, const gchar *reason);
void
fu_idle_locker_free(FuIdleLocker *locker);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuIdleLocker, fu_idle_locker_free)
