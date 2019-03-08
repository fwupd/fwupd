/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/errno.h>
#include <string.h>
#include <unistd.h>

#include "fu-superio-common.h"

gboolean
fu_superio_outb (gint fd, guint16 port, guint8 data, GError **error)
{
	if (pwrite (fd, &data, 1, (goffset) port) != 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write to port %04x: %s",
			     (guint) port,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_superio_inb (gint fd, guint16 port, guint8 *data, GError **error)
{
	if (pread (fd, data, 1, (goffset) port) != 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to read from port %04x: %s",
			     (guint) port,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_superio_regval (gint fd, guint16 port, guint8 addr,
		   guint8 *data, GError **error)
{
	if (!fu_superio_outb (fd, port, addr, error))
		return FALSE;
	if (!fu_superio_inb (fd, port + 1, data, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_superio_regval16 (gint fd, guint16 port, guint8 addr,
		     guint16 *data, GError **error)
{
	guint8 msb;
	guint8 lsb;
	if (!fu_superio_regval (fd, port, addr, &msb, error))
		return FALSE;
	if (!fu_superio_regval (fd, port, addr + 1, &lsb, error))
		return FALSE;
	*data = ((guint16) msb << 8) | (guint16) lsb;
	return TRUE;
}

gboolean
fu_superio_regwrite (gint fd, guint16 port, guint8 addr,
		     guint8 data, GError **error)
{
	if (!fu_superio_outb (fd, port, addr, error))
		return FALSE;
	if (!fu_superio_outb (fd, port + 1, data, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_superio_set_ldn (gint fd, guint16 port, guint8 ldn, GError **error)
{
	return fu_superio_regwrite (fd, port, SIO_LDNxx_IDX_LDNSEL, ldn, error);
}

const gchar *
fu_superio_ldn_to_text (guint8 ldn)
{
	if (ldn == SIO_LDN_FDC)
		return "Floppy Disk Controller";
	if (ldn == SIO_LDN_GPIO)
		return "General Purpose IO";
	if (ldn == SIO_LDN_PARALLEL_PORT)
		return "Parallel Port";
	if (ldn == SIO_LDN_UART1)
		return "Serial Port 1";
	if (ldn == SIO_LDN_UART2)
		return "Serial Port 2";
	if (ldn == SIO_LDN_UART3)
		return "Serial Port 3";
	if (ldn == SIO_LDN_UART4)
		return "Serial Port 4";
	if (ldn == SIO_LDN_SWUC)
		return "System Wake-Up Control";
	if (ldn == SIO_LDN_KBC_MOUSE)
		return "KBC/Mouse";
	if (ldn == SIO_LDN_KBC_KEYBOARD)
		return "KBC/Keyboard";
	if (ldn == SIO_LDN_CIR)
		return "Consumer IR";
	if (ldn == SIO_LDN_SMFI)
		return "Shared Memory/Flash";
	if (ldn == SIO_LDN_RTCT)
		return "RTC-like Timer";
	if (ldn == SIO_LDN_SSSP1)
		return "Serial Peripheral";
	if (ldn == SIO_LDN_PECI)
		return "Platform Environmental Control";
	if (ldn == SIO_LDN_PM1)
		return "Power Management 1";
	if (ldn == SIO_LDN_PM2)
		return "Power Management 2";
	if (ldn == SIO_LDN_PM3)
		return "Power Management 3";
	if (ldn == SIO_LDN_PM4)
		return "Power Management 4";
	if (ldn == SIO_LDN_PM5)
		return "Power Management 5";
	return NULL;
}

gboolean
fu_superio_regdump (gint fd, guint16 port, guint8 ldn, GError **error)
{
	const gchar *ldnstr = fu_superio_ldn_to_text (ldn);
	guint8 buf[0xff] = { 0x00 };
	guint16 iobad0 = 0x0;
	guint16 iobad1 = 0x0;
	g_autoptr(GString) str = g_string_new (NULL);

	/* set LDN */
	if (!fu_superio_set_ldn (fd, port, ldn, error))
		return FALSE;
	for (guint i = 0x00; i < 0xff; i++) {
		if (!fu_superio_regval (fd, port, i, &buf[i], error))
			return FALSE;
	}

	/* get the i/o base addresses */
	if (!fu_superio_regval16 (fd, port, SIO_LDNxx_IDX_IOBAD0, &iobad0, error))
		return FALSE;
	if (!fu_superio_regval16 (fd, port, SIO_LDNxx_IDX_IOBAD1, &iobad1, error))
		return FALSE;

	g_string_append_printf (str, "PORT:0x%04x ", port);
	g_string_append_printf (str, "LDN:0x%02x ", ldn);
	if (iobad0 != 0x0)
		g_string_append_printf (str, "IOBAD0:0x%04x ", iobad0);
	if (iobad1 != 0x0)
		g_string_append_printf (str, "IOBAD1:0x%04x ", iobad1);
	if (ldnstr != NULL)
		g_string_append_printf (str, "(%s)", ldnstr);
	fu_common_dump_raw (G_LOG_DOMAIN, str->str, buf, 0x100);
	return TRUE;
}
