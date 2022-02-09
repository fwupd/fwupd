/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#define ICH9_REG_BFPR	0x00
#define ICH9_REG_HSFS	0x04
#define ICH9_REG_HSFC	0x06
#define ICH9_REG_FADDR	0x08
#define ICH9_REG_RESRVD 0x0C
#define ICH9_REG_FDATA0 0x10
#define ICH9_REG_FDATAN 0x14

#define ICH9_REG_FRAP  0x50
#define ICH9_REG_FREG0 0x54
#define ICH9_REG_PR0   0x74
#define ICH9_REG_FDOC  0xB0
#define ICH9_REG_FDOD  0xB4

#define PCH100_REG_FDOC 0xB4
#define PCH100_REG_FDOD 0xB8
#define PCH100_REG_FPR0 0x84
#define PCH100_REG_GPR0 0x98

#define PCH100_FADDR_FLA 0x07ffffff

#define PCH100_HSFC_FCYCLE (0xf << 1)

#define FDOC_FDSI (0x3F << 2)
#define FDOC_FDSS (0x03 << 12)

#define HSFS_FDONE   (0x01 << 0)
#define HSFS_FCERR   (0x01 << 1)
#define HSFS_AEL     (0x01 << 2)
#define HSFS_BERASE  (0x03 << 3)
#define HSFS_SCIP    (0x01 << 5)
#define HSFS_FDOPSS  (0x01 << 13)
#define HSFS_FDV     (0x01 << 14)
#define HSFS_FLOCKDN (0x01 << 15)

#define HSFC_FGO    (0x01 << 0)
#define HSFC_FCYCLE (0x03 << 1)
#define HSFC_FDBC   (0x3f << 8)
#define HSFC_SME    (0x01 << 15)

typedef enum {
	FU_INTEL_SPI_KIND_UNKNOWN,
	FU_INTEL_SPI_KIND_APL,
	FU_INTEL_SPI_KIND_C620,
	FU_INTEL_SPI_KIND_ICH0,
	FU_INTEL_SPI_KIND_ICH2345,
	FU_INTEL_SPI_KIND_ICH6,
	FU_INTEL_SPI_KIND_ICH9,
	FU_INTEL_SPI_KIND_PCH100,
	FU_INTEL_SPI_KIND_PCH200,
	FU_INTEL_SPI_KIND_PCH300,
	FU_INTEL_SPI_KIND_PCH400,
	FU_INTEL_SPI_KIND_POULSBO,
	FU_INTEL_SPI_KIND_LAST
} FuIntelSpiKind;

FuIntelSpiKind
fu_intel_spi_kind_from_string(const gchar *kind);
const gchar *
fu_intel_spi_kind_to_string(FuIntelSpiKind kind);

guint16
fu_mmio_read16(gconstpointer addr, goffset offset);
void
fu_mmio_write16(gpointer addr, goffset offset, guint16 val);

guint32
fu_mmio_read32(gconstpointer addr, goffset offset);
void
fu_mmio_write32(gpointer addr, goffset offset, guint32 val);

guint32
fu_mmio_read32_le(gconstpointer addr, goffset offset);
void
fu_mmio_write32_le(gpointer addr, goffset offset, guint32 val);
