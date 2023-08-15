/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPROM_DEVICE (fu_synaprom_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapromDevice, fu_synaprom_device, FU, SYNAPROM_DEVICE, FuUsbDevice)

#define FU_SYNAPROM_PRODUCT_PROMETHEUS	   65 /* Prometheus (b1422) */
#define FU_SYNAPROM_PRODUCT_PROMETHEUSPBL  66
#define FU_SYNAPROM_PRODUCT_PROMETHEUSMSBL 67
#define FU_SYNAPROM_PRODUCT_TRITON	   69 /* Triton */
#define FU_SYNAPROM_PRODUCT_TRITONPBL	   70
#define FU_SYNAPROM_PRODUCT_TRITONMSBL	   71

/**
 * Product type definitions
 */
#define FU_SYNAPROM_PRODUCT_TYPE_DENALI	      0
#define FU_SYNAPROM_PRODUCT_TYPE_HAYES	      1
#define FU_SYNAPROM_PRODUCT_TYPE_SHASTA	      2
#define FU_SYNAPROM_PRODUCT_TYPE_STELLER      3
#define FU_SYNAPROM_PRODUCT_TYPE_WHITNEY      4
#define FU_SYNAPROM_PRODUCT_TYPE_PROMETHEUS   5
#define FU_SYNAPROM_PRODUCT_TYPE_PACIFIC_PEAK 6
#define FU_SYNAPROM_PRODUCT_TYPE_MORGAN	      7
#define FU_SYNAPROM_PRODUCT_TYPE_OX6101	      8
#define FU_SYNAPROM_PRODUCT_TYPE_TRITON	      9

#define FU_SYNAPROM_CMD_GET_VERSION   0x01
#define FU_SYNAPROM_CMD_BOOTLDR_PATCH 0x7d
#define FU_SYNAPROM_CMD_IOTA_FIND     0x8e

FuSynapromDevice *
fu_synaprom_device_new(FuUsbDevice *device);
gboolean
fu_synaprom_device_cmd_send(FuSynapromDevice *device,
			    GByteArray *request,
			    GByteArray *reply,
			    FuProgress *progress,
			    guint timeout_ms,
			    GError **error);
gboolean
fu_synaprom_device_write_fw(FuSynapromDevice *self,
			    GBytes *fw,
			    FuProgress *progress,
			    GError **error);

/* for self tests */
void
fu_synaprom_device_set_version(FuSynapromDevice *self,
			       guint8 vmajor,
			       guint8 vminor,
			       guint32 buildnum);
FuFirmware *
fu_synaprom_device_prepare_fw(FuDevice *device,
			      GBytes *fw,
			      FwupdInstallFlags flags,
			      GError **error);

guint32
fu_synaprom_device_get_product_type(FuSynapromDevice *self);
