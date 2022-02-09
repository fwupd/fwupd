/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#include "fu-intel-spi-common.h"

FuIntelSpiKind
fu_intel_spi_kind_from_string(const gchar *kind)
{
	if (g_strcmp0(kind, "ich9") == 0)
		return FU_INTEL_SPI_KIND_ICH9;
	if (g_strcmp0(kind, "pch100") == 0)
		return FU_INTEL_SPI_KIND_PCH100;
	if (g_strcmp0(kind, "apl") == 0)
		return FU_INTEL_SPI_KIND_APL;
	if (g_strcmp0(kind, "c620") == 0)
		return FU_INTEL_SPI_KIND_C620;
	if (g_strcmp0(kind, "ich0") == 0)
		return FU_INTEL_SPI_KIND_ICH0;
	if (g_strcmp0(kind, "ich2345") == 0)
		return FU_INTEL_SPI_KIND_ICH2345;
	if (g_strcmp0(kind, "ich6") == 0)
		return FU_INTEL_SPI_KIND_ICH6;
	if (g_strcmp0(kind, "pch200") == 0)
		return FU_INTEL_SPI_KIND_PCH200;
	if (g_strcmp0(kind, "pch300") == 0)
		return FU_INTEL_SPI_KIND_PCH300;
	if (g_strcmp0(kind, "pch400") == 0)
		return FU_INTEL_SPI_KIND_PCH400;
	if (g_strcmp0(kind, "poulsbo") == 0)
		return FU_INTEL_SPI_KIND_POULSBO;
	return FU_INTEL_SPI_KIND_UNKNOWN;
}

const gchar *
fu_intel_spi_kind_to_string(FuIntelSpiKind kind)
{
	if (kind == FU_INTEL_SPI_KIND_ICH9)
		return "ich9";
	if (kind == FU_INTEL_SPI_KIND_PCH100)
		return "pch100";
	if (kind == FU_INTEL_SPI_KIND_APL)
		return "apl";
	if (kind == FU_INTEL_SPI_KIND_C620)
		return "c620";
	if (kind == FU_INTEL_SPI_KIND_ICH0)
		return "ich0";
	if (kind == FU_INTEL_SPI_KIND_ICH2345)
		return "ich2345";
	if (kind == FU_INTEL_SPI_KIND_ICH6)
		return "ich6";
	if (kind == FU_INTEL_SPI_KIND_PCH200)
		return "pch200";
	if (kind == FU_INTEL_SPI_KIND_PCH300)
		return "pch300";
	if (kind == FU_INTEL_SPI_KIND_PCH400)
		return "pch400";
	if (kind == FU_INTEL_SPI_KIND_POULSBO)
		return "poulsbo";
	return NULL;
}

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
