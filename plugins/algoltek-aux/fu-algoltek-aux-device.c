/*
 * Copyright (C) 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-algoltek-aux-common.h"
#include "fu-algoltek-aux-device.h"
#include "fu-algoltek-aux-firmware.h"
#include "fu-algoltek-aux-struct.h"

struct _FuAlgoltekAuxDevice {
	FuDpauxDevice parent_instance;
	guint8 testoui;
};

G_DEFINE_TYPE(FuAlgoltekAuxDevice, fu_algoltek_aux_device, FU_TYPE_DPAUX_DEVICE)

FuAlgoltekAuxDevice *
fu_algoltek_aux_device_new(FuDpauxDevice *device)
{
	FuAlgoltekAuxDevice *self = g_object_new(FU_TYPE_ALGOLTEK_AUX_DEVICE, NULL);
	if (device != NULL)
		fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device));
	return self;
}

static gboolean
fu_algoltek_dpaux_device_write(FuAlgoltekAuxDevice *self, GByteArray *buf, guint delayms, GError **error)
{
	fu_device_sleep(FU_DEVICE(self), delayms);
	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				  0x80000,
				  buf->data,
				  buf->len,
				  ALGOLTEK_DEVICE_AUX_TIMEOUT,
				  error))
		return FALSE;
	
	return TRUE;
}

static gboolean
fu_algoltek_dpaux_device_read(FuAlgoltekAuxDevice *self, GByteArray *buf, GError **error)
{
	fu_device_sleep(FU_DEVICE(self), 20);
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  0x80010,
				  buf->data,
				  buf->len,
				  ALGOLTEK_DEVICE_AUX_TIMEOUT,
				  error))
		return FALSE;
	
	return TRUE;
}


static guint16
gen_crc16(guint16 BData, guint16 crc)
{
	guint16 BFlag = 0;

	for (guint16 i= 0 ; i < 8; i++ ) {
		BFlag = BData ^ (crc >> 8);
		crc <<= 1;
		
		if (BFlag & 0x80) {
			crc ^= FU_ALGOLTEK_AUX_CMD_CRC_POLINOM;
			
		}
		BData <<= 1;
		
	}
	
	return crc;
}

static GByteArray *
fu_algoltek_aux_device_rdv(FuAlgoltekAuxDevice *self, GError **error)
{	
	g_autoptr(GByteArray) replay = g_byte_array_new();
	g_autoptr(GByteArray) version_data = g_byte_array_new();
	guint copyCount = 0;

	fu_byte_array_set_size(replay, 16, 0x0);
	fu_byte_array_set_size(version_data, 64, 0x0);

	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_rdv_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_sublen(st, length);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_address(st, 0);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_RDV);

	for (guint i = 0; i < 4; i++) {
	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error)){
		g_prefix_error(error, "\naux dpcd write failed:\n");
		return "aux dpcd rdv failed:" ;
	}

	if(!fu_algoltek_dpaux_device_read(FU_DPAUX_DEVICE(self), replay, error)){
		g_prefix_error(error, "\naux dpcd read failed:\n");
		return "aux dpcd rdv failed:" ;
	}

	if (i == 0) {
		memcpy(version_data->data + copyCount, replay->data + 2, 14);
		copyCount += 14;	
	} else {
		memcpy(version_data->data + copyCount, replay->data + 0, 16);
		copyCount += 16;
	}
	g_print(replay->data);
	}
	
	/* success */
	return g_steal_pointer(&version_data);
}


static gboolean
fu_algoltek_aux_device_en(FuAlgoltekAuxDevice *self, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_new();
	gsize length = st->len - 3;
	
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_sublen(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_EN);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_address(st, 0);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_value(st, 0);
	
	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error))
		return FALSE;	
		
	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_aux_device_rst(FuAlgoltekAuxDevice *self, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_new();
	gsize length = st->len - 3;
	
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_sublen(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_RST);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_address(st, 0x300);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_value(st, 0);

	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error))
		return FALSE;	

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_aux_device_dummy(FuAlgoltekAuxDevice *self, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_rdv_cmd_address_pkt_new();

	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_sublen(st, 0);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_address(st, 0);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_len(st, 0);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_cmd(st, 0);

	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error))
		return FALSE;	
		

	/* success */
	return TRUE;
}
static gboolean
fu_algoltek_aux_device_wrr(FuAlgoltekAuxDevice *self, int address, int inputValue, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_sublen(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_WRR);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_address(st, address);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_value(st, inputValue);
	g_print("\nWRITE");

	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error))
		return FALSE;	
		
	
	/* success */
	return TRUE;
}


static GByteArray *
fu_algoltek_aux_device_ispcrc(FuAlgoltekAuxDevice *self, int serialNo, guint16* WCRC, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_crc_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_sublen(st, st->len | 0x80);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_serialno(st, serialNo);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_len(st, st->len);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_ISP);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_wcrc(st, *WCRC);

	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error))
		return FALSE;	
	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_aux_device_isp(FuAlgoltekAuxDevice *self, GInputStream *stream, guint16* WCRC, FuProgress *progress, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_new();
	gsize length = st->len - 3;
	guint16 serialNo = 0;
	
	g_autoptr(FuChunkArray) chunks = NULL;
	chunks = fu_chunk_array_new_from_stream(stream, 0, 8, error);
	if (chunks == NULL)			
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_i2c_address(st, 0x51);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_sublen(st, length);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_serialno(st, serialNo);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_len(st, length);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_ISP);
		if (!fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_data(st,
									fu_chunk_get_data(chk),
									fu_chunk_get_data_sz(chk),
									error)) {
				g_prefix_error(error, "assign isp data failure: ");
				return FALSE;
			}
		
		for (guint16 crc_i = 0 ; crc_i < 8; crc_i++ ) {
			*WCRC = gen_crc16(fu_chunk_get_data(chk)[crc_i], *WCRC);
		}
		
		if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error))
			return FALSE;
		
		serialNo += 1;
		
		if ((i + 1) % 32 == 0) {
			if (!fu_algoltek_aux_device_ispcrc(self, serialNo, WCRC, error))
				return FALSE;
			*WCRC = FU_ALGOLTEK_AUX_CMD_CRC_INIT_POLINOM;
			serialNo += 1;
			
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_aux_device_bot(FuAlgoltekAuxDevice *self, int address, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_sublen(st, length + 1);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_BOT);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_address(st, address);

	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error))
		return FALSE;	

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_aux_device_ers(FuAlgoltekAuxDevice *self, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_sublen(st, length + 1);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_ERS);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_address(st, 0x6000);

	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 20, error))
		return FALSE;	

	/* success */
	return TRUE;
}


static GByteArray *
fu_algoltek_aux_device_wrfcrc(FuAlgoltekAuxDevice *self, int serialNo, guint16* WCRC, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_crc_cmd_address_pkt_new();
	gsize length = st->len - 3;
    
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_i2c_address(st, 0x51);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_sublen(st, length | 0x80);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_serialno(st, serialNo);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_len(st, 0x04);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_ISP);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_wcrc(st, *WCRC);
	
	if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 10, error))
		return FALSE;	

	/* success */
	return TRUE;

}

static gboolean
fu_algoltek_aux_device_wrf(FuAlgoltekAuxDevice *self, GInputStream *stream, guint16* WCRC, FuProgress *progress, GError **error)
{	
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_new();
	gsize length = st->len - 3;
    guint16 serialNo = 1;
    guint8 startLength = 0;
	guint firmwareBufferCount = 0;

	g_autoptr(FuChunkArray) chunks = NULL;
	chunks = fu_chunk_array_new_from_stream(stream, 0, 8, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	g_autoptr(GByteArray) firmwareBytesArray = NULL;
	firmwareBytesArray = fu_input_stream_read_byte_array(stream, 0, AG_FIRMWARE_AUX_SIZE, error);
	if (firmwareBytesArray == NULL) {
		return FALSE;
	} 

	for (guint i = 0; i < fu_chunk_array_length(chunks) ; i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_i2c_address(st, 0x51);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_sublen(st, length | startLength);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_serialno(st, serialNo);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_len(st, length - 1);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_WRF);

		for (guint16 i= 0 ; i < 8; i++ ) {
			st->data[i + 6] = firmwareBytesArray->data[i + firmwareBufferCount];
			*WCRC = gen_crc16(st->data[i + 6], *WCRC);
		}
		
		if(!fu_algoltek_dpaux_device_write(FU_DPAUX_DEVICE(self), st, 10, error))
			return FALSE;
		fu_algoltek_aux_device_dummy(self, error);
		firmwareBufferCount += 8;
        serialNo += 1;

        if ((i + 1) % 32 == 31) {
            startLength = 0x40;
        } else {
            startLength = 0x00;
        }

        if ((i + 1) % 32 == 0) {
			g_print("\nRamWrite: %d",  serialNo);
			//g_print("\nWRFCRC %x", *WCRC);
            if (!fu_algoltek_aux_device_wrfcrc(self, serialNo, WCRC, error))
				return FALSE;
			fu_algoltek_aux_device_dummy(self, error);
			*WCRC = FU_ALGOLTEK_AUX_CMD_CRC_INIT_POLINOM;
            serialNo += 1;
        }
		fu_progress_step_done(progress);
	}
	/* success */
	return TRUE;
}


static gboolean
fu_algoltek_aux_device_write_firmware(FuDevice *device, FuFirmware *firmware, FuProgress *progress, FwupdInstallFlags flags, GError **error)
{	
	FuAlgoltekAuxDevice *self = FU_ALGOLTEK_AUX_DEVICE(device);
	guint32 firmwareBufferCount = 0;
	guint16 WCRC = 0x1021;

	g_autoptr(GInputStream) stream_isp = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 18, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);

	if (!fu_algoltek_aux_device_en(self, error))
		return FALSE;
	
	if (!fu_algoltek_aux_device_rst(self, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 500);

	if (!fu_algoltek_aux_device_wrr(self, 0x80AD, 0, error))
		return FALSE;

	if (!fu_algoltek_aux_device_wrr(self, 0x80C0, 0, error))
		return FALSE;

	if (!fu_algoltek_aux_device_wrr(self, 0x80C9, 0, error))
		return FALSE;

	if (!fu_algoltek_aux_device_wrr(self, 0x80D1, 0, error))
		return FALSE;

	if (!fu_algoltek_aux_device_wrr(self, 0x80D9, 0, error))
		return FALSE;

	if (!fu_algoltek_aux_device_wrr(self, 0x80E1, 0, error))
		return FALSE;

	if (!fu_algoltek_aux_device_wrr(self, 0x80E9, 0, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 20);

	// /* get ISP image */
	stream_isp = fu_firmware_get_image_by_id_stream(firmware, "isp", error);
	if (stream_isp == NULL)
		return FALSE;
	
    if (!fu_algoltek_aux_device_isp(self, stream_isp, &WCRC, fu_progress_get_child(progress), error))
				return FALSE;

	fu_device_sleep(FU_DEVICE(self), 2000);
#if 1
	if (!fu_algoltek_aux_device_bot(self, 0x6000, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 2000);

	if (!fu_algoltek_aux_device_ers(self, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 5000);

	/* get payload image */
	stream_payload = fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (stream_payload == NULL)
		return FALSE;


#if 1
    if (!fu_algoltek_aux_device_wrf(self, stream_payload, &WCRC, fu_progress_get_child(progress), error))
	    return FALSE;

#endif
	
#endif
	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_aux_device_setup(FuDevice *device, FuFirmware *firmware, GError **error)
{
	FuAlgoltekAuxDevice *self = FU_ALGOLTEK_AUX_DEVICE(device);
	g_autofree gchar *version_str = NULL;
	g_autoptr(GByteArray) version_data = NULL;
	g_autoptr(GByteArray) guid_data = g_byte_array_new();
	fu_byte_array_set_size(guid_data, 4, 0x0);

	/* AuxDevice->setup */
	if (!FU_DEVICE_CLASS(fu_algoltek_aux_device_parent_class)->setup(device, error))
		return FALSE;

	version_data = fu_algoltek_aux_device_rdv(self, error);

	g_print("\nSETUP\n");
	if (version_data == NULL) {
		return NULL;
	}

	version_str = g_strdup_printf("%s", version_data->data);

	fu_device_set_version(FU_DEVICE(self), version_str);
	g_autofree gchar *product = NULL;
	
	product = g_strdup_printf("MST-ALGOLTEK");
	
	
	fu_device_add_instance_id(FU_DEVICE(self), product);
	
	/* success */
	return TRUE;
}


static void
fu_algoltek_aux_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_algoltek_aux_device_init(FuAlgoltekAuxDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.algoltek.aux");
	fu_device_add_vendor_id(FU_DEVICE(self), "DRM_DP_AUX_DEV:0x25A4");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE); //Selected device to upgrade
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ALGOLTEK_AUX_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
}


static void
fu_algoltek_aux_device_class_init(FuAlgoltekAuxDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_algoltek_aux_device_setup;
	klass_device->write_firmware = fu_algoltek_aux_device_write_firmware;
	klass_device->set_progress = fu_algoltek_aux_device_set_progress;
}
