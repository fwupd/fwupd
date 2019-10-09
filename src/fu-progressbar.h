/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fwupd-enums.h"

#define FU_TYPE_PROGRESSBAR (fu_progressbar_get_type ())
G_DECLARE_FINAL_TYPE (FuProgressbar, fu_progressbar, FU, PROGRESSBAR, GObject)

FuProgressbar	*fu_progressbar_new			(void);
void		 fu_progressbar_update			(FuProgressbar	*self,
							 FwupdStatus	 status,
							 guint		 percentage);
void		 fu_progressbar_set_length_status	(FuProgressbar	*self,
							 guint		 len);
void		 fu_progressbar_set_length_percentage	(FuProgressbar	*self,
							 guint		 len);
void		 fu_progressbar_set_title		(FuProgressbar	*self,
							 const gchar	*title);
void		 fu_progressbar_set_interactive		(FuProgressbar *self,
							 gboolean interactive);
