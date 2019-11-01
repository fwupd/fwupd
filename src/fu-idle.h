/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <fwupd.h>

#include "fu-device.h"

#define FU_TYPE_IDLE (fu_idle_get_type ())
G_DECLARE_FINAL_TYPE (FuIdle, fu_idle, FU, IDLE, GObject)

FuIdle		*fu_idle_new			(void);
guint32		 fu_idle_inhibit		(FuIdle		*self,
						 const gchar	*reason);
void		 fu_idle_uninhibit		(FuIdle		*self,
						 guint32	 token);
void		 fu_idle_set_timeout		(FuIdle		*self,
						 guint		 timeout);
void		 fu_idle_reset			(FuIdle		*self);
FwupdStatus	 fu_idle_get_status		(FuIdle		*self);

/**
 * FuIdleLocker:
 * @idle:	A #FuIdle
 * @token:	A #guint32 number
 *
 * A locker to prevent daemon from shutting down on its own
 **/
typedef struct {
	FuIdle		*idle;
	guint32		 token;
} FuIdleLocker;

FuIdleLocker	*fu_idle_locker_new		(FuIdle		*self,
						 const gchar	*reason);
void		 fu_idle_locker_free		(FuIdleLocker	*locker);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuIdleLocker, fu_idle_locker_free)
