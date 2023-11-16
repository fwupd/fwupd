/*
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Apollo Ling <apollo.ling@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_MST_CONNECTION (fu_synaptics_mst_connection_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsMstConnection,
		     fu_synaptics_mst_connection,
		     FU,
		     SYNAPTICS_MST_CONNECTION,
		     GObject)

#define ADDR_CUSTOMER_ID 0X10E
#define ADDR_BOARD_ID	 0x10F

#define ADDR_MEMORY_CUSTOMER_ID_CAYENNE 0x9000024E
#define ADDR_MEMORY_BOARD_ID_CAYENNE	0x9000024F
#define ADDR_MEMORY_CUSTOMER_ID_SPYDER	0x9000020E
#define ADDR_MEMORY_BOARD_ID_SPYDER	0x9000020F
#define ADDR_MEMORY_CUSTOMER_ID		0x170E
#define ADDR_MEMORY_BOARD_ID		0x170F

#define REG_CHIP_ID	     0x507
#define REG_FIRMWARE_VERSION 0x50A

FuSynapticsMstConnection *
fu_synaptics_mst_connection_new(FuIOChannel *io_channel, guint8 layer, guint rad);

gboolean
fu_synaptics_mst_connection_read(FuSynapticsMstConnection *self,
				 guint32 offset,
				 guint8 *buf,
				 gsize bufsz,
				 GError **error);

gboolean
fu_synaptics_mst_connection_rc_set_command(FuSynapticsMstConnection *self,
					   guint32 rc_cmd,
					   guint32 offset,
					   const guint8 *buf,
					   gsize bufsz,
					   GError **error);

gboolean
fu_synaptics_mst_connection_rc_get_command(FuSynapticsMstConnection *self,
					   guint32 rc_cmd,
					   guint32 offset,
					   guint8 *buf,
					   gsize bufsz,
					   GError **error);

gboolean
fu_synaptics_mst_connection_rc_special_get_command(FuSynapticsMstConnection *self,
						   guint32 rc_cmd,
						   guint32 cmd_offset,
						   guint8 *cmd_data,
						   gsize cmd_datasz,
						   guint8 *buf,
						   gsize bufsz,
						   GError **error);
