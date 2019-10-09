/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2019 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define RMI_PRODUCT_ID_LENGTH				10
#define RMI_DEVICE_PDT_ENTRY_SIZE			6

typedef struct {
	guint16		 query_base;
	guint16		 command_base;
	guint16		 control_base;
	guint16		 data_base;
	guint8		 interrupt_source_count;
	guint8		 function_number;
	guint8		 function_version;
	guint8		 interrupt_reg_num;
	guint8		 interrupt_mask;
} FuSynapticsRmiFunction;

guint32		 fu_synaptics_rmi_generate_checksum	(const guint8	*data,
							 gsize		 len);
FuSynapticsRmiFunction *fu_synaptics_rmi_function_parse	(GByteArray	*buf,
							 guint16	 page_base,
							 guint		 interrupt_count,
							 GError		**error);
gboolean	 fu_synaptics_rmi_device_writeln	(const gchar	*fn,
							 const gchar	*buf,
							 GError		**error);
