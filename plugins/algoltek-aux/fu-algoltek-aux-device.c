/*
 * Copyright 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-aux-device.h"
#include "fu-algoltek-aux-firmware.h"
#include "fu-algoltek-aux-struct.h"

struct _FuAlgoltekAuxDevice {
	FuDpauxDevice parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekAuxDevice, fu_algoltek_aux_device, FU_TYPE_DPAUX_DEVICE)

#define FU_ALGOLTEK_DEVICE_AUX_TIMEOUT 3000 /* ms */

#define FU_ALGOLTEK_AUX_UPDATE_STATUS 0x860C
#define FU_ALGOLTEK_AUX_UPDATE_PASS   1
#define FU_ALGOLTEK_AUX_UPDATE_FAIL   2

#define FU_ALGOLTEK_AUX_CRC_INIT_POLINOM 0x1021
#define FU_ALGOLTEK_AUX_CRC_POLINOM	 0x1021

static gboolean
fu_algoltek_aux_device_write(FuAlgoltekAuxDevice *self,
			     GByteArray *buf,
			     guint delayms,
			     GError **error)
{
	fu_device_sleep(FU_DEVICE(self), delayms);
	return fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				     0x80000,
				     buf->data,
				     buf->len,
				     FU_ALGOLTEK_DEVICE_AUX_TIMEOUT,
				     error);
}

static gboolean
fu_algoltek_aux_device_read(FuAlgoltekAuxDevice *self, GByteArray *buf, GError **error)
{
	fu_device_sleep(FU_DEVICE(self), 20);
	return fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				    0x80010,
				    buf->data,
				    buf->len,
				    FU_ALGOLTEK_DEVICE_AUX_TIMEOUT,
				    error);
}

static guint16
fu_algoltek_aux_device_crc16_step(guint16 val, guint16 crc)
{
	for (guint16 i = 0; i < 8; i++) {
		guint16 bflag = val ^ (crc >> 8);
		crc <<= 1;
		if (bflag & 0x80) {
			crc ^= FU_ALGOLTEK_AUX_CRC_POLINOM;
		}
		val <<= 1;
	}
	return crc;
}

static guint16
fu_algoltek_aux_device_crc16(const guint8 *buf, gsize bufsz, guint16 crc)
{
	for (gsize i = 0; i < bufsz; i++)
		crc = fu_algoltek_aux_device_crc16_step(buf[i], crc);
	return crc;
}

static GByteArray *
fu_algoltek_aux_device_rdv(FuAlgoltekAuxDevice *self, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_rdv_cmd_address_pkt_new();
	g_autoptr(GByteArray) reply = g_byte_array_new();
	g_autoptr(GByteArray) version_data = g_byte_array_new();
	guint copy_count = 0;
	gsize length = st->len - 3;

	fu_byte_array_set_size(reply, 16, 0x0);
	fu_byte_array_set_size(version_data, 64, 0x0);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_sublen(st, length);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_rdv_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_RDV);

	for (guint i = 0; i < 4; i++) {
		if (!fu_algoltek_aux_device_write(self, st, 20, error)) {
			g_prefix_error(error, "aux dpcd write failed: ");
			return NULL;
		}
		if (!fu_algoltek_aux_device_read(self, reply, error)) {
			g_prefix_error(error, "aux dpcd read failed: ");
			return NULL;
		}
		if (i == 0) {
			if (!fu_memcpy_safe(version_data->data,
					    version_data->len,
					    copy_count, /* dst */
					    reply->data,
					    reply->len,
					    2, /* src */
					    14,
					    error))
				return NULL;
			copy_count += 14;
		} else {
			if (!fu_memcpy_safe(version_data->data,
					    version_data->len,
					    copy_count, /* dst */
					    reply->data,
					    reply->len,
					    0, /* src */
					    16,
					    error))
				return NULL;
			copy_count += 16;
		}
	}

	/* success */
	return g_steal_pointer(&version_data);
}

static gboolean
fu_algoltek_aux_device_en(FuAlgoltekAuxDevice *self, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_sublen(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_EN);
	return fu_algoltek_aux_device_write(self, st, 20, error);
}

static gboolean
fu_algoltek_aux_device_rst(FuAlgoltekAuxDevice *self, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_sublen(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_RST);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_address(st, 0x300);
	return fu_algoltek_aux_device_write(self, st, 20, error);
}

static gboolean
fu_algoltek_aux_device_dummy(FuAlgoltekAuxDevice *self, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_rdv_cmd_address_pkt_new();
	return fu_algoltek_aux_device_write(self, st, 20, error);
}

static gboolean
fu_algoltek_aux_device_wrr(FuAlgoltekAuxDevice *self, int address, int inputValue, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_sublen(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_WRR);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_address(st, address);
	fu_struct_algoltek_aux_en_rst_wrr_cmd_address_pkt_set_value(st, inputValue);
	return fu_algoltek_aux_device_write(self, st, 20, error);
}

static gboolean
fu_algoltek_aux_device_ispcrc(FuAlgoltekAuxDevice *self,
			      guint16 serialno,
			      guint16 wcrc,
			      GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_crc_cmd_address_pkt_new();
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_sublen(st, st->len | 0x80);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_serialno(st, serialno);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_len(st, st->len);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_ISP);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_wcrc(st, wcrc);
	return fu_algoltek_aux_device_write(self, st, 20, error);
}

static gboolean
fu_algoltek_aux_device_isp(FuAlgoltekAuxDevice *self,
			   GInputStream *stream,
			   guint16 *wcrc,
			   FuProgress *progress,
			   GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	guint16 serialno = 0;

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						8,
						error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(GByteArray) st =
		    fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_new();
		gsize length = st->len - 3;
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_sublen(st, length);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_serialno(st, serialno);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_len(st, length);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_cmd(
		    st,
		    FU_ALGOLTEK_AUX_CMD_ISP);
		if (!fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_data(
			st,
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			error)) {
			g_prefix_error(error, "assign isp data failure: ");
			return FALSE;
		}

		if (fu_chunk_get_data_sz(chk) < 8) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "incomplete chunk");
			return FALSE;
		}
		*wcrc = fu_algoltek_aux_device_crc16(fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     *wcrc);
		if (!fu_algoltek_aux_device_write(self, st, 20, error))
			return FALSE;

		serialno += 1;
		if ((i + 1) % 32 == 0) {
			if (!fu_algoltek_aux_device_ispcrc(self, serialno, *wcrc, error))
				return FALSE;
			*wcrc = FU_ALGOLTEK_AUX_CRC_INIT_POLINOM;
			serialno += 1;
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

	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_sublen(st, length + 1);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_BOT);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_address(st, address);
	return fu_algoltek_aux_device_write(self, st, 20, error);
}

static gboolean
fu_algoltek_aux_device_ers(FuAlgoltekAuxDevice *self, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_sublen(st, length + 1);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_len(st, length);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_ERS);
	fu_struct_algoltek_aux_bot_ers_cmd_address_pkt_set_address(st, 0x6000);
	return fu_algoltek_aux_device_write(self, st, 20, error);
}

static gboolean
fu_algoltek_aux_device_wrfcrc(FuAlgoltekAuxDevice *self,
			      guint16 serialno,
			      guint16 wcrc,
			      GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_crc_cmd_address_pkt_new();
	gsize length = st->len - 3;

	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_sublen(st, length | 0x80);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_serialno(st, serialno);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_len(st, 0x04);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_cmd(st, FU_ALGOLTEK_AUX_CMD_ISP);
	fu_struct_algoltek_aux_crc_cmd_address_pkt_set_wcrc(st, wcrc);
	return fu_algoltek_aux_device_write(self, st, 10, error);
}

static gboolean
fu_algoltek_aux_device_wrf(FuAlgoltekAuxDevice *self,
			   GInputStream *stream,
			   guint16 *wcrc,
			   FuProgress *progress,
			   GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_new();
	g_autoptr(FuChunkArray) chunks = NULL;
	gsize length = st->len - 3;
	guint8 start_length = 0;
	guint16 serialno = 1;

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						8,
						error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_sublen(st,
										  length |
										      start_length);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_serialno(st, serialno);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_len(st, length - 1);
		fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_cmd(
		    st,
		    FU_ALGOLTEK_AUX_CMD_WRF);
		if (!fu_struct_algoltek_aux_isp_flash_write_cmd_address_pkt_set_data(
			st,
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			error))
			return FALSE;
		*wcrc = fu_algoltek_aux_device_crc16(fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     *wcrc);
		if (!fu_algoltek_aux_device_write(self, st, 10, error))
			return FALSE;
		if (!fu_algoltek_aux_device_dummy(self, error))
			return FALSE;
		serialno += 1;

		if ((i + 1) % 32 == 31) {
			start_length = 0x40;
		} else {
			start_length = 0x00;
		}
		if ((i + 1) % 32 == 0) {
			if (!fu_algoltek_aux_device_wrfcrc(self, serialno, *wcrc, error))
				return FALSE;
			if (!fu_algoltek_aux_device_dummy(self, error))
				return FALSE;
			*wcrc = FU_ALGOLTEK_AUX_CRC_INIT_POLINOM;
			serialno += 1;
		}
		fu_progress_step_done(progress);
	}
	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_aux_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuAlgoltekAuxDevice *self = FU_ALGOLTEK_AUX_DEVICE(device);
	guint16 wcrc = FU_ALGOLTEK_AUX_CRC_INIT_POLINOM;
	g_autoptr(GInputStream) stream_isp = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;

	/* progress */
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 18, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "isp");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "payload");

	/* prepare hardware? */
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
	fu_progress_step_done(progress);

	/* write ISP image */
	stream_isp = fu_firmware_get_image_by_id_stream(firmware, "isp", error);
	if (stream_isp == NULL)
		return FALSE;
	if (!fu_algoltek_aux_device_isp(self,
					stream_isp,
					&wcrc,
					fu_progress_get_child(progress),
					error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 2000);
	if (!fu_algoltek_aux_device_bot(self, 0x6000, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 2000);
	if (!fu_algoltek_aux_device_ers(self, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 5000);
	fu_progress_step_done(progress);

	/* write payload image */
	stream_payload =
	    fu_firmware_get_image_by_id_stream(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_algoltek_aux_device_wrf(self,
					stream_payload,
					&wcrc,
					fu_progress_get_child(progress),
					error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_aux_device_setup(FuDevice *device, GError **error)
{
	FuAlgoltekAuxDevice *self = FU_ALGOLTEK_AUX_DEVICE(device);
	g_autofree gchar *product = NULL;
	g_autofree gchar *version_str = NULL;
	g_autoptr(GByteArray) version_data = NULL;

	/* FuAuxDevice->setup */
	if (!FU_DEVICE_CLASS(fu_algoltek_aux_device_parent_class)->setup(device, error))
		return FALSE;

	/* get current version */
	version_data = fu_algoltek_aux_device_rdv(self, error);
	if (version_data == NULL)
		return FALSE;
	version_str = fu_strsafe((const gchar *)version_data->data, version_data->len);
	fu_device_set_version(FU_DEVICE(self), version_str);

	/* build something unique as a GUID */
	fu_device_add_instance_strup(device, "VEN", "25A4");
	product = fu_strsafe((const gchar *)version_data->data, 6);
	fu_device_add_instance_strup(device, "DEV", product);
	if (!fu_device_build_instance_id(device, error, "MST", "VEN", "DEV", NULL))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_algoltek_aux_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
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
	fu_device_build_vendor_id_u16(FU_DEVICE(self), "DRM_DP_AUX_DEV", 0x25A4);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
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
