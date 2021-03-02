/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-synaptics-rmi-common.h"
#include "fu-udev-device.h"

#define FU_TYPE_SYNAPTICS_RMI_DEVICE (fu_synaptics_rmi_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuSynapticsRmiDevice, fu_synaptics_rmi_device, FU, SYNAPTICS_RMI_DEVICE, FuUdevDevice)

typedef enum {
	FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE		= 0,
	FU_SYNAPTICS_RMI_DEVICE_FLAG_ALLOW_FAILURE	= 1 << 0,
} FuSynapticsRmiDeviceFlags;

struct _FuSynapticsRmiDeviceClass
{
	FuUdevDeviceClass	parent_class;
	gboolean		 (*setup)			(FuSynapticsRmiDevice	*self,
								 GError			**error);
	gboolean		 (*query_status)		(FuSynapticsRmiDevice	*self,
								 GError			**error);
	gboolean		 (*write)			(FuSynapticsRmiDevice	*self,
								 guint16		 addr,
								 GByteArray		*req,
								 FuSynapticsRmiDeviceFlags flags,
								 GError			**error);
	GByteArray		*(*read)			(FuSynapticsRmiDevice	*self,
								 guint16		 addr,
								 gsize			 req_sz,
								 GError			**error);
	GByteArray		*(*read_packet_register)	(FuSynapticsRmiDevice	*self,
								 guint16		 addr,
								 gsize			 req_sz,
								 GError			**error);
	gboolean		 (*wait_for_attr)		(FuSynapticsRmiDevice	*self,
								 guint8			 source_mask,
								 guint			 timeout_ms,
								 GError			**error);
	gboolean		 (*set_page)			(FuSynapticsRmiDevice	*self,
								 guint8			 page,
								 GError			**error);
	gboolean		 (*disable_sleep)		(FuSynapticsRmiDevice	 *self,
								 GError			**error);
	gboolean		 (*write_bus_select)		(FuSynapticsRmiDevice	 *self,
								 guint8			 bus,
								 GError			**error);
	gboolean		 (*query_build_id)		(FuSynapticsRmiDevice	*self,
								 guint32		*build_id,
								 GError			**error);
	gboolean		 (*query_product_sub_id)	(FuSynapticsRmiDevice	 *self,
								 guint8			*product_sub_id,
								 GError			**error);
	gboolean		 (*enter_iep_mode)		(FuSynapticsRmiDevice	 *self,
								 GError			**error);
};

typedef struct {
	guint16			 block_count_cfg;
	guint16			 block_count_fw;
	guint16			 block_size;
	guint16			 config_length;
	guint16			 payload_length;
	guint32			 build_id;
	guint8			 bootloader_id[2];
	guint8			 status_addr;
} FuSynapticsRmiFlash;

#define RMI_F34_HAS_NEW_REG_MAP				(1 << 0)
#define RMI_F34_HAS_CONFIG_ID				(1 << 2)

#define RMI_F34_BLOCK_DATA_OFFSET			2
#define RMI_F34_BLOCK_DATA_V1_OFFSET			1

#define RMI_F34_ENABLE_WAIT_MS				300		/* ms */
#define RMI_F34_IDLE_WAIT_MS				500		/* ms */

#define RMI_DEVICE_PAGE_SELECT_REGISTER			0xff
#define RMI_DEVICE_BUS_SELECT_REGISTER			0xfe

typedef enum {
	RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE		= 0,
	RMI_DEVICE_WAIT_FOR_IDLE_FLAG_REFRESH_F34	= (1 << 0),
} RmiDeviceWaitForIdleFlags;

void		 	 fu_synaptics_rmi_device_set_iepmode	(FuSynapticsRmiDevice	*self,
								 gboolean		iepmode);
gboolean		 fu_synaptics_rmi_device_set_page	(FuSynapticsRmiDevice	*self,
								 guint8			 page,
								 GError			**error);
gboolean		 fu_synaptics_rmi_device_write_bootloader_id	(FuSynapticsRmiDevice	*self,
								 GError			**error);
gboolean		 fu_synaptics_rmi_device_disable_irqs	(FuSynapticsRmiDevice	*self,
								 GError			**error);
GByteArray		*fu_synaptics_rmi_device_read		(FuSynapticsRmiDevice	*self,
								 guint16		 addr,
								 gsize			 req_sz,
								 GError			**error);
GByteArray		*fu_synaptics_rmi_device_read_packet_register (FuSynapticsRmiDevice	*self,
								 guint16		 addr,
								 gsize			 req_sz,
								 GError			**error);
gboolean		 fu_synaptics_rmi_device_write		(FuSynapticsRmiDevice	*self,
								 guint16		 addr,
								 GByteArray		*req,
								 FuSynapticsRmiDeviceFlags flags,
								 GError			**error);
gboolean		 fu_synaptics_rmi_device_reset		(FuSynapticsRmiDevice	*self,
								 GError			**error);
gboolean		 fu_synaptics_rmi_device_wait_for_idle	(FuSynapticsRmiDevice	*self,
								 guint			 timeout_ms,
								 RmiDeviceWaitForIdleFlags flags,
								 GError			**error);
gboolean		 fu_synaptics_rmi_device_disable_sleep	(FuSynapticsRmiDevice	*self,
								 GError			**error);
FuSynapticsRmiFlash	*fu_synaptics_rmi_device_get_flash	(FuSynapticsRmiDevice	*self);
FuSynapticsRmiFunction	*fu_synaptics_rmi_device_get_function	(FuSynapticsRmiDevice	*self,
								 guint8			 function_number,
								 GError			**error);
gboolean		 fu_synaptics_rmi_device_poll_wait	(FuSynapticsRmiDevice	*self,
								 GError			**error);
void			 fu_synaptics_rmi_device_set_sig_size	(FuSynapticsRmiDevice	*self,
								 guint16		 sig_size);
guint16			 fu_synaptics_rmi_device_get_sig_size	(FuSynapticsRmiDevice	*self);
void			 fu_synaptics_rmi_device_set_max_page	(FuSynapticsRmiDevice	*self,
								 guint8			 max_page);
guint8			 fu_synaptics_rmi_device_get_max_page	(FuSynapticsRmiDevice	*self);
gboolean		 fu_synaptics_rmi_device_enter_iep_mode	(FuSynapticsRmiDevice	*self,
								 GError			**error);
gboolean		 fu_synaptics_rmi_device_write_bus_select (FuSynapticsRmiDevice *self,
								 guint8			 bus,
								 GError			**error);
