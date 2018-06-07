/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_PROGRESSBAR_H
#define __FU_PROGRESSBAR_H

#include <gio/gio.h>

#include "fwupd-enums.h"

G_BEGIN_DECLS

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

G_END_DECLS

#endif /* __FU_PROGRESSBAR_H */

