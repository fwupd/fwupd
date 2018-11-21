/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_IDLE_H
#define __FU_IDLE_H

G_BEGIN_DECLS

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

typedef struct {
	FuIdle		*idle;
	guint32		 token;
} FuIdleLocker;

FuIdleLocker	*fu_idle_locker_new		(FuIdle		*self,
						 const gchar	*reason);
void		 fu_idle_locker_free		(FuIdleLocker	*locker);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuIdleLocker, fu_idle_locker_free)

G_END_DECLS

#endif /* __FU_IDLE_H */

