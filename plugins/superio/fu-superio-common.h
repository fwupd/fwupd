/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

G_BEGIN_DECLS

/* for all LDNs */
#define SIO_LDNxx_IDX_LDNSEL		0x07
#define SIO_LDNxx_IDX_CHIPID1		0x20
#define SIO_LDNxx_IDX_CHIPID2		0x21
#define SIO_LDNxx_IDX_CHIPVER		0x22
#define SIO_LDNxx_IDX_SIOCTRL		0x23
#define SIO_LDNxx_IDX_SIOIRQ		0x25
#define SIO_LDNxx_IDX_SIOGP		0x26
#define SIO_LDNxx_IDX_SIOPWR		0x2d
#define SIO_LDNxx_IDX_D2ADR		0x2e
#define SIO_LDNxx_IDX_D2DAT		0x2f

#define SIO_LDNxx_IDX_IOBAD0		0x60 /* 16 bit */
#define SIO_LDNxx_IDX_IOBAD1		0x62 /* 16 bit */

typedef enum {
	SIO_LDN_FDC			= 0x00,	/* IT87 */
	SIO_LDN_UART1			= 0x01,	/* IT87+IT89 */
	SIO_LDN_UART2			= 0x02,	/* IT87+IT89 */
	SIO_LDN_PARALLEL_PORT		= 0x03,	/* IT87 */
	SIO_LDN_SWUC			= 0x04,	/* IT87+IT89 */
	SIO_LDN_KBC_MOUSE		= 0x05,	/* IT87+IT89 */
	SIO_LDN_KBC_KEYBOARD		= 0x06,	/* IT87+IT89 */
	SIO_LDN_GPIO			= 0x07,	/* IT87 */
	SIO_LDN_UART3			= 0x08,	/* IT87 */
	SIO_LDN_UART4			= 0x09,	/* IT87 */
	SIO_LDN_CIR			= 0x0a,	/* IT89 */
	SIO_LDN_SMFI			= 0x0f,	/* IT89 */
	SIO_LDN_RTCT			= 0x10,	/* IT89 */
	SIO_LDN_PM1			= 0x11,	/* IT89 */
	SIO_LDN_PM2			= 0x12,	/* IT89 */
	SIO_LDN_SSSP1			= 0x13,	/* IT89 */
	SIO_LDN_PECI			= 0x14,	/* IT89 */
	SIO_LDN_PM3			= 0x17,	/* IT89 */
	SIO_LDN_PM4			= 0x18,	/* IT89 */
	SIO_LDN_PM5			= 0x19,	/* IT89 */
	SIO_LDN_LAST			= 0x1a
} SioLdn;

const gchar	*fu_superio_ldn_to_text	(guint8		 ldn);
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
