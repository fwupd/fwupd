/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#include "fu-intel-spi-common.h"

guint16
fu_mmio_read16(gconstpointer addr, goffset offset)
{
	addr = (guint8 *)addr + offset;
	return *(volatile const guint16 *)addr;
}

guint32
fu_mmio_read32(gconstpointer addr, goffset offset)
{
	addr = (guint8 *)addr + offset;
	return *(volatile const guint32 *)addr;
}

void
fu_mmio_write16(gpointer addr, goffset offset, guint16 val)
{
	addr = (guint8 *)addr + offset;
	*(volatile guint16 *)addr = val;
}

void
fu_mmio_write32(gpointer addr, goffset offset, guint32 val)
{
	addr = (guint8 *)addr + offset;
	*(volatile guint32 *)addr = val;
}

guint32
fu_mmio_read32_le(gconstpointer addr, goffset offset)
{
	return GUINT32_FROM_LE(fu_mmio_read32(addr, offset));
}

void
fu_mmio_write32_le(gpointer addr, goffset offset, guint32 val)
{
	fu_mmio_write32(addr, offset, GUINT32_TO_LE(val));
}
