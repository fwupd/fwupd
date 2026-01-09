/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-impl.h"

G_DEFINE_INTERFACE(FuLenovoAccessoryImpl, fu_lenovo_accessory_impl, G_TYPE_OBJECT)

static void
fu_lenovo_accessory_impl_default_init(FuLenovoAccessoryImplInterface *iface)
{
}

#if 0
static GByteArray *
fu_lenovo_accessory_impl_read(FuLenovoAccessoryImpl *self, GError **error)
{
	FuLenovoAccessoryImplInterface *iface;

	g_return_val_if_fail(FU_IS_LENOVO_ACCESSORY_IMPL(self), NULL);

	iface = FU_LENOVO_ACCESSORY_IMPL_GET_IFACE(self);
	if (iface->read == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->read not implemented");
		return NULL;
	}
	return (*iface->read)(self, error);
}
#endif

static gboolean
fu_lenovo_accessory_impl_write(FuLenovoAccessoryImpl *self, GByteArray *buf, GError **error)
{
	FuLenovoAccessoryImplInterface *iface;

	g_return_val_if_fail(FU_IS_LENOVO_ACCESSORY_IMPL(self), FALSE);

	iface = FU_LENOVO_ACCESSORY_IMPL_GET_IFACE(self);
	if (iface->write == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->write not implemented");
		return FALSE;
	}
	return (*iface->write)(self, buf, error);
}

static GByteArray *
fu_lenovo_accessory_impl_process(FuLenovoAccessoryImpl *self, GByteArray *buf, GError **error)
{
	FuLenovoAccessoryImplInterface *iface;

	g_return_val_if_fail(FU_IS_LENOVO_ACCESSORY_IMPL(self), NULL);

	iface = FU_LENOVO_ACCESSORY_IMPL_GET_IFACE(self);
	if (iface->process == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->process not implemented");
		return NULL;
	}
	return (*iface->process)(self, buf, error);
}

gboolean
fu_lenovo_accessory_impl_get_fwversion(FuLenovoAccessoryImpl *self,
				       guint8 *major,
				       guint8 *minor,
				       guint8 *micro,
				       GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoFwVersionRsp) st_rsp = NULL;

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x03);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_INFO_ID_FIRMWARE_VERSION |
		(FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));
	buf = fu_lenovo_accessory_impl_process(self, st_cmd->buf, error);
	if (buf == NULL)
		return FALSE;
	st_rsp = fu_struct_lenovo_fw_version_rsp_parse(buf->data, buf->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	if (major != NULL)
		*major = fu_struct_lenovo_fw_version_rsp_get_major(st_rsp);
	if (minor != NULL)
		*minor = fu_struct_lenovo_fw_version_rsp_get_minor(st_rsp);
	if (micro != NULL)
		*micro = fu_struct_lenovo_fw_version_rsp_get_internal(st_rsp);
	return TRUE;
}

#if 0
gboolean
fu_lenovo_accessory_impl_get_mode(FuLenovoAccessoryImpl *self, guint8 *mode, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoDevicemodeRsp) st_rsp = NULL;
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x01);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_INFO_ID_DEVICE_MODE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	buf = fu_lenovo_accessory_impl_process(self, st_cmd->buf, error);
	if (buf == NULL)
		return FALSE;
	st_rsp = fu_struct_lenovo_devicemode_rsp_parse(buf->data, buf->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	*mode = fu_struct_lenovo_devicemode_rsp_get_mode(st_rsp);

	/* success */
	return TRUE;
}
#endif

gboolean
fu_lenovo_accessory_impl_set_mode(FuLenovoAccessoryImpl *self, guint8 mode, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoDevicemodeReq) st_req = fu_struct_lenovo_devicemode_req_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x01);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_INFO_ID_DEVICE_MODE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_devicemode_req_set_cmd(st_req, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_devicemode_req_set_mode(st_req, mode);
	if (mode == 0x02) {
		return fu_lenovo_accessory_impl_write(self, st_req->buf, error);
	}
	buf = fu_lenovo_accessory_impl_process(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

/* @exit_code: the exit status code (e.g., 0x00 for success/reboot) */
gboolean
fu_lenovo_accessory_impl_dfu_exit(FuLenovoAccessoryImpl *self, guint8 exit_code, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoDfuExitReq) st_req = fu_struct_lenovo_dfu_exit_req_new();
	g_autoptr(GError) error_local = NULL;

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x01);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_EXIT | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_dfu_exit_req_set_cmd(st_req, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_dfu_exit_req_set_exit_code(st_req, exit_code);

	/*
	 * The device performs an immediate reset/reboot as soon as it receives the
	 * DFU_EXIT command and therefore never sends back an ACK. The resulting
	 * error (e.g., -EPIPE or -EIO) is expected and indicates that the reboot
	 * was successfully triggered.
	 */
	if (!fu_lenovo_accessory_impl_write(self, st_req->buf, &error_local)) {
		g_debug("ignoring: %s", error_local->message);
	}

	/* success */
	return TRUE;
}

gboolean
fu_lenovo_accessory_impl_dfu_attribute(FuLenovoAccessoryImpl *self,
				       guint8 *major_ver,
				       guint8 *minor_ver,
				       guint16 *product_pid,
				       guint8 *processor_id,
				       guint32 *app_max_size,
				       guint32 *page_size,
				       GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoDfuAttributeRsp) st_rsp = NULL;
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x0D);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_ATTRIBUTE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));
	buf = fu_lenovo_accessory_impl_process(self, st_cmd->buf, error);
	if (buf == NULL)
		return FALSE;
	st_rsp = fu_struct_lenovo_dfu_attribute_rsp_parse(buf->data, buf->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;

	/* success */
	if (major_ver != NULL)
		*major_ver = fu_struct_lenovo_dfu_attribute_rsp_get_major_ver(st_rsp);
	if (minor_ver != NULL)
		*minor_ver = fu_struct_lenovo_dfu_attribute_rsp_get_minor_ver(st_rsp);
	if (product_pid != NULL)
		*product_pid = fu_struct_lenovo_dfu_attribute_rsp_get_product_pid(st_rsp);
	if (processor_id != NULL)
		*processor_id = fu_struct_lenovo_dfu_attribute_rsp_get_processor_id(st_rsp);
	if (app_max_size != NULL)
		*app_max_size = fu_struct_lenovo_dfu_attribute_rsp_get_app_max_size(st_rsp);
	if (page_size != NULL)
		*page_size = fu_struct_lenovo_dfu_attribute_rsp_get_page_size(st_rsp);
	return TRUE;
}

gboolean
fu_lenovo_accessory_impl_dfu_prepare(FuLenovoAccessoryImpl *self,
				     guint8 file_type,
				     guint32 start_address,
				     guint32 end_address,
				     guint32 crc32,
				     GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoDfuPrepareReq) st_req = fu_struct_lenovo_dfu_prepare_req_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x0D);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_PREPARE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_dfu_prepare_req_set_cmd(st_req, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_dfu_prepare_req_set_file_type(st_req, file_type);
	fu_struct_lenovo_dfu_prepare_req_set_start_address(st_req, start_address);
	fu_struct_lenovo_dfu_prepare_req_set_end_address(st_req, end_address);
	fu_struct_lenovo_dfu_prepare_req_set_crc32(st_req, crc32);
	buf = fu_lenovo_accessory_impl_process(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
fu_lenovo_accessory_impl_dfu_file(FuLenovoAccessoryImpl *self,
				  guint8 file_type,
				  guint32 address,
				  const guint8 *data,
				  gsize datasz,
				  GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoDfuFwReq) st_req = fu_struct_lenovo_dfu_fw_req_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, datasz + 5);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_FILE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_dfu_fw_req_set_cmd(st_req, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_dfu_fw_req_set_file_type(st_req, file_type);
	fu_struct_lenovo_dfu_fw_req_set_offset_address(st_req, address);
	if (!fu_struct_lenovo_dfu_fw_req_set_data(st_req, data, datasz, error))
		return FALSE;
	buf = fu_lenovo_accessory_impl_process(self, st_req->buf, error);
	if (buf == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
fu_lenovo_accessory_impl_dfu_crc(FuLenovoAccessoryImpl *self, guint32 *crc32, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoDfuCrcRsp) st_rsp = NULL;
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x05);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_CRC | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));

	buf = fu_lenovo_accessory_impl_process(self, st_cmd->buf, error);
	if (buf == NULL)
		return FALSE;
	st_rsp = fu_struct_lenovo_dfu_crc_rsp_parse(buf->data, buf->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;

	/* success */
	if (crc32 != NULL)
		*crc32 = fu_struct_lenovo_dfu_crc_rsp_get_crc32(st_rsp);
	return TRUE;
}

gboolean
fu_lenovo_accessory_impl_dfu_entry(FuLenovoAccessoryImpl *self, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(GByteArray) buf = NULL;

	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_ENTRY | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	buf = fu_lenovo_accessory_impl_process(self, st_cmd->buf, error);
	if (buf == NULL)
		return FALSE;

	/* success */
	return TRUE;
}
