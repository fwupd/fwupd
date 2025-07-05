/*
 * Copyright 2021 Sean Rhodes <sean@starlabs.systems>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#ifdef HAVE_IO_H
#include <sys/io.h>
#endif

#include "fu-flashrom-cmos.h"

#ifdef HAVE_IO_H
static gboolean
fu_flashrom_cmos_write(guint8 addr, guint8 val)
{
	guint8 tmp;

	/* Reject addresses in the second bank */
	if (addr >= 128)
		return FALSE;

	/* Write the value to CMOS */
	outb(addr, RTC_BASE_PORT);
	outb(val, RTC_BASE_PORT + 1);

	/* Read the value back from CMOS */
	outb(addr, RTC_BASE_PORT);
	tmp = inb(RTC_BASE_PORT + 1);

	/* Check the read value against the written */
	return (tmp == val);
}
#endif

gboolean
fu_flashrom_cmos_reset(GError **error)
{
#ifdef HAVE_IO_H
	/* Call ioperm() to grant us access to ports 0x70 and 0x71 */
	if (ioperm(RTC_BASE_PORT, 2, TRUE) < 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to gain access to ports 0x70 and 0x71");
		return FALSE;
	}

	/* Write a default value to the CMOS checksum */
	if ((!fu_flashrom_cmos_write(CMOS_CHECKSUM_OFFSET, 0xff)) ||
	    (!fu_flashrom_cmos_write(CMOS_CHECKSUM_OFFSET + 1, 0xff))) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "failed to reset CMOS");
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no <sys/io.h> support");
	return FALSE;
#endif
}
