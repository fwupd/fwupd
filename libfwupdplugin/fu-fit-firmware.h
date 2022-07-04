/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-fdt-firmware.h"

#define FU_TYPE_FIT_FIRMWARE (fu_fit_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuFitFirmware, fu_fit_firmware, FU, FIT_FIRMWARE, FuFdtFirmware)

struct _FuFitFirmwareClass {
	FuFdtFirmwareClass parent_class;
};

FuFirmware *
fu_fit_firmware_new(void);
guint32
fu_fit_firmware_get_timestamp(FuFitFirmware *self);
void
fu_fit_firmware_set_timestamp(FuFitFirmware *self, guint32 timestamp);

/**
 * FU_FIT_FIRMWARE_ATTR_COMPATIBLE:
 *
 * The compatible metadata for the FIT image, typically a string list, e.g.
 *`pine64,rockpro64-v2.1:pine64,rockpro64`.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_COMPATIBLE "compatible"

/**
 * FU_FIT_FIRMWARE_ATTR_DATA:
 *
 * The raw data for the FIT image, typically a blob.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_DATA "data"

/**
 * FU_FIT_FIRMWARE_ATTR_ALGO:
 *
 * The checksum algorithm for the FIT image, typically a string, e.g. `crc32`.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_ALGO "algo"

/**
 * FU_FIT_FIRMWARE_ATTR_DATA_OFFSET:
 *
 * The external data offset after the FIT image, typically a uint32.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_DATA_OFFSET "data-offset"

/**
 * FU_FIT_FIRMWARE_ATTR_DATA_SIZE:
 *
 * The data size of the external image, typically a uint32.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_DATA_SIZE "data-size"

/**
 * FU_FIT_FIRMWARE_ATTR_STORE_OFFSET:
 *
 * The store offset for the FIT image, typically a uint32.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_STORE_OFFSET "store-offset"

/**
 * FU_FIT_FIRMWARE_ATTR_VALUE:
 *
 * The value of the checksum, which is typically a blob.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_VALUE "value"

/**
 * FU_FIT_FIRMWARE_ATTR_SKIP_OFFSET:
 *
 * The offset to skip when writing the FIT image, typically a uint32.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_SKIP_OFFSET "skip-offset"

/**
 * FU_FIT_FIRMWARE_ATTR_VERSION:
 *
 * The version of the FIT image, typically a string, e.g. `1.2.3`.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_VERSION "version"

/**
 * FU_FIT_FIRMWARE_ATTR_TIMESTAMP:
 *
 * The creation timestamp of FIT image, typically a uint32.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ATTR_TIMESTAMP "timestamp"

/**
 * FU_FIT_FIRMWARE_ID_IMAGES:
 *
 * The usual firmware ID string for the images.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ID_IMAGES "images"

/**
 * FU_FIT_FIRMWARE_ID_CONFIGURATIONS:
 *
 * The usual firmware ID string for the configurations.
 *
 * Since: 1.8.2
 **/
#define FU_FIT_FIRMWARE_ID_CONFIGURATIONS "configurations"
