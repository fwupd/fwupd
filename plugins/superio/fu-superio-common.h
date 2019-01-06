/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_SUPERIO_COMMON_H
#define __FU_SUPERIO_COMMON_H

#include "fu-plugin.h"

G_BEGIN_DECLS

#define LDN_SEL			0x07	/* LDN select register */
#define MAXLDN			0x14	/* maximum LDN */

gboolean	 fu_superio_outb	(gint		 fd,
					 guint16	 port,
					 guint8		 data,
					 GError		**error);
gboolean	 fu_superio_inb		(gint		 fd,
					 guint16	 port,
					 guint8		*data,
					 GError		**error);
gboolean	 fu_superio_regval	(gint		 fd,
					 guint16	 port,
					 guint8		 addr,
					 guint8		*data,
					 GError		**error);
gboolean	 fu_superio_regval16	(gint		 fd,
					 guint16	 port,
					 guint8		 addr,
					 guint16	*data,
					 GError		**error);
gboolean	 fu_superio_regwrite	(gint		 fd,
					 guint16	 port,
					 guint8		 addr,
					 guint8		 data,
					 GError		**error);
gboolean	 fu_superio_regdump	(gint		 fd,
					 guint16	 port,
					 guint8		 ldn,
					 GError		**error);
gboolean	 fu_superio_set_ldn	(gint		 fd,
					 guint16	 port,
					 guint8		 ldn,
					 GError		**error);

G_END_DECLS

#endif /* __FU_SUPERIO_COMMON_H */
