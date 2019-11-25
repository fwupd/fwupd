/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <xmlb.h>

#include "fu-archive.h"
#include "fu-chunk.h"
#include "fu-fastboot-device.h"

#define FASTBOOT_REMOVE_DELAY_RE_ENUMERATE	60000 /* ms */
#define FASTBOOT_TRANSACTION_TIMEOUT		1000 /* ms */
#define FASTBOOT_TRANSACTION_RETRY_MAX		600
#define FASTBOOT_EP_IN				0x81
#define FASTBOOT_EP_OUT				0x01
#define FASTBOOT_CMD_BUFSZ			64 /* bytes */

struct _FuFastbootDevice {
	FuUsbDevice			 parent_instance;
	gboolean			 secure;
	guint				 blocksz;
	guint8				 intf_nr;
};

G_DEFINE_TYPE (FuFastbootDevice, fu_fastboot_device, FU_TYPE_USB_DEVICE)

static void
fu_fastboot_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuFastbootDevice *self = FU_FASTBOOT_DEVICE (device);
	fu_common_string_append_kx (str, idt, "InterfaceNumber", self->intf_nr);
	fu_common_string_append_kx (str, idt, "BlockSize", self->blocksz);
	fu_common_string_append_kb (str, idt, "Secure", self->secure);
}

static gboolean
fu_fastboot_device_probe (FuDevice *device, GError **error)
{
	FuFastbootDevice *self = FU_FASTBOOT_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	g_autoptr(GUsbInterface) intf = NULL;

	/* find the correct fastboot interface */
	intf = g_usb_device_get_interface (usb_device, 0xff, 0x42, 0x03, error);
	if (intf == NULL)
		return FALSE;
	self->intf_nr = g_usb_interface_get_number (intf);
	return TRUE;
}

static gboolean
fu_fastboot_device_open (FuUsbDevice *device, GError **error)
{
	FuFastbootDevice *self = FU_FASTBOOT_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	if (!g_usb_device_claim_interface (usb_device, self->intf_nr,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_fastboot_buffer_dump (const gchar *title, const guint8 *buf, gsize sz)
{
	if (g_getenv ("FWUPD_FASTBOOT_VERBOSE") == NULL)
		return;
	g_print ("%s (%" G_GSIZE_FORMAT "):\n", title, sz);
	for (gsize i = 0; i < sz; i++) {
		g_print ("%02x[%c] ", buf[i], g_ascii_isprint (buf[i]) ? buf[i] : '?');
		if (i > 0 && (i + 1) % 256 == 0)
			g_print ("\n");
	}
	g_print ("\n");
}

static gboolean
fu_fastboot_device_write (FuDevice *device, const guint8 *buf, gsize buflen, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	gboolean ret;
	gsize actual_len = 0;
	g_autofree guint8 *buf2 = g_memdup (buf, (guint) buflen);

	fu_fastboot_buffer_dump ("writing", buf, buflen);
	ret = g_usb_device_bulk_transfer (usb_device,
					  FASTBOOT_EP_OUT,
					  buf2,
					  buflen,
					  &actual_len,
					  FASTBOOT_TRANSACTION_TIMEOUT,
					  NULL, error);
	if (!ret) {
		g_prefix_error (error, "failed to do bulk transfer: ");
		return FALSE;
	}
	if (actual_len != buflen) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "only wrote %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_fastboot_device_writestr (FuDevice *device, const gchar *str, GError **error)
{
	gsize buflen = strlen (str);
	if (buflen > FASTBOOT_CMD_BUFSZ - 4) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "fastboot limits writes to %i bytes",
			     FASTBOOT_CMD_BUFSZ - 4);
		return FALSE;
	}
	return fu_fastboot_device_write (device, (const guint8 *) str, buflen, error);
}

typedef enum {
	FU_FASTBOOT_DEVICE_READ_FLAG_NONE,
	FU_FASTBOOT_DEVICE_READ_FLAG_STATUS_POLL,
} FuFastbootDeviceReadFlags;

static gboolean
fu_fastboot_device_read (FuDevice *device,
			 gchar **str,
			 FuFastbootDeviceReadFlags flags,
			 GError **error)
{
	FuFastbootDevice *self = FU_FASTBOOT_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	guint retries = 1;

	/* these commands may return INFO or take some time to complete */
	if (flags & FU_FASTBOOT_DEVICE_READ_FLAG_STATUS_POLL)
		retries = FASTBOOT_TRANSACTION_RETRY_MAX;

	for (guint i = 0; i < retries; i++) {
		gboolean ret;
		gsize actual_len = 0;
		guint8 buf[FASTBOOT_CMD_BUFSZ] = { 0x00 };
		g_autofree gchar *tmp = NULL;
		g_autoptr(GError) error_local = NULL;

		ret = g_usb_device_bulk_transfer (usb_device,
						  FASTBOOT_EP_IN,
						  buf,
						  sizeof(buf),
						  &actual_len,
						  FASTBOOT_TRANSACTION_TIMEOUT,
						  NULL, &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     G_USB_DEVICE_ERROR,
					     G_USB_DEVICE_ERROR_TIMED_OUT)) {
				g_debug ("ignoring %s", error_local->message);
				continue;
			}
			g_propagate_prefixed_error (error,
						    g_steal_pointer (&error_local),
						    "failed to do bulk transfer: ");
			return FALSE;
		}
		fu_fastboot_buffer_dump ("read", buf, actual_len);
		if (actual_len < 4) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "only read %" G_GSIZE_FORMAT "bytes", actual_len);
			return FALSE;
		}

		/* info */
		tmp = g_strndup ((const gchar *) buf + 4, self->blocksz - 4);
		if (memcmp (buf, "INFO", 4) == 0) {
			if (g_strcmp0 (tmp, "erasing flash") == 0)
				fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
			else if (g_strcmp0 (tmp, "writing flash") == 0)
				fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
			else
				g_debug ("INFO returned unknown: %s", tmp);
			continue;
		}

		/* success */
		if (memcmp (buf, "OKAY", 4) == 0 || memcmp (buf, "DATA", 4) == 0) {
			if (str != NULL)
				*str = g_steal_pointer (&tmp);
			return TRUE;
		}

		/* failure */
		if (memcmp (buf, "FAIL", 4) == 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to read response: %s", tmp);
			return FALSE;
		}

		/* unknown failure */
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "failed to read response");
		return FALSE;
	}

	/* we timed out a *lot* */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "no response to read");
	return FALSE;
}

static gboolean
fu_fastboot_device_getvar (FuDevice *device, const gchar *key, gchar **str, GError **error)
{
	g_autofree gchar *tmp = g_strdup_printf ("getvar:%s", key);
	if (!fu_fastboot_device_writestr (device, tmp, error))
		return FALSE;
	if (!fu_fastboot_device_read (device, str,
				      FU_FASTBOOT_DEVICE_READ_FLAG_NONE, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_fastboot_device_cmd (FuDevice *device, const gchar *cmd,
			FuFastbootDeviceReadFlags flags, GError **error)
{
	if (!fu_fastboot_device_writestr (device, cmd, error))
		return FALSE;
	if (!fu_fastboot_device_read (device, NULL, flags, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_fastboot_device_flash (FuDevice *device, const gchar *partition, GError **error)
{
	g_autofree gchar *tmp = g_strdup_printf ("flash:%s", partition);
	return fu_fastboot_device_cmd (device, tmp,
				       FU_FASTBOOT_DEVICE_READ_FLAG_STATUS_POLL,
				       error);
}

static gboolean
fu_fastboot_device_download (FuDevice *device, GBytes *fw, GError **error)
{
	FuFastbootDevice *self = FU_FASTBOOT_DEVICE (device);
	gsize sz = g_bytes_get_size (fw);
	g_autofree gchar *tmp = g_strdup_printf ("download:%08x", (guint) sz);
	g_autoptr(GPtrArray) chunks = NULL;

	/* tell the client the size of data to expect */
	if (!fu_fastboot_device_cmd (device, tmp,
				     FU_FASTBOOT_DEVICE_READ_FLAG_STATUS_POLL,
				     error))
		return FALSE;

	/* send the data in chunks */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						self->blocksz);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_fastboot_device_write (device, chk->data, chk->data_sz, error))
			return FALSE;
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len * 2);
	}
	if (!fu_fastboot_device_read (device, NULL,
				      FU_FASTBOOT_DEVICE_READ_FLAG_STATUS_POLL, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_fastboot_device_setup (FuDevice *device, GError **error)
{
	FuFastbootDevice *self = FU_FASTBOOT_DEVICE (device);
	g_autofree gchar *product = NULL;
	g_autofree gchar *serialno = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *secure = NULL;
	g_autofree gchar *version_bootloader = NULL;

	/* product */
	if (!fu_fastboot_device_getvar (device, "product", &product, error))
		return FALSE;
	if (product != NULL && product[0] != '\0') {
		g_autofree gchar *tmp = g_strdup_printf ("Fastboot %s", product);
		fu_device_set_name (device, tmp);
	}

	/* fastboot API version */
	if (!fu_fastboot_device_getvar (device, "version", &version, error))
		return FALSE;
	if (version != NULL && version[0] != '\0')
		g_debug ("fastboot version=%s", version);

	/* bootloader version */
	if (!fu_fastboot_device_getvar (device, "version-bootloader", &version_bootloader, error))
		return FALSE;
	if (version_bootloader != NULL && version_bootloader[0] != '\0')
		fu_device_set_version_bootloader (device, version_bootloader);

	/* serialno */
	if (!fu_fastboot_device_getvar (device, "serialno", &serialno, error))
		return FALSE;
	if (serialno != NULL && serialno[0] != '\0')
		fu_device_set_serial (device, serialno);

	/* secure */
	if (!fu_fastboot_device_getvar (device, "secure", &secure, error))
		return FALSE;
	if (secure != NULL && secure[0] != '\0')
		self->secure = TRUE;

	/* success */
	return TRUE;
}

static gboolean
fu_fastboot_device_write_qfil_part (FuDevice *device,
				    FuArchive *archive,
				    XbNode *part,
				    GError **error)
{
	GBytes *data;
	const gchar *fn;
	const gchar *partition;

	/* not all partitions have images */
	fn = xb_node_query_text (part, "img_name", NULL);
	if (fn == NULL)
		return TRUE;

	/* find filename */
	data = fu_archive_lookup_by_fn (archive, fn, error);
	if (data == NULL)
		return FALSE;

	/* get the partition name */
	partition = xb_node_query_text (part, "name", error);
	if (partition == NULL)
		return FALSE;
	if (g_str_has_prefix (partition, "0:"))
		partition += 2;

	/* flash the partition */
	if (!fu_fastboot_device_download (device, data, error))
		return FALSE;
	return fu_fastboot_device_flash (device, partition, error);
}

static gboolean
fu_fastboot_device_write_motorola_part (FuDevice *device,
					FuArchive *archive,
					XbNode *part,
					GError **error)
{
	const gchar *op = xb_node_get_attr (part, "operation");

	/* oem */
	if (g_strcmp0 (op, "oem") == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "OEM commands are not supported");
		return FALSE;
	}

	/* getvar */
	if (g_strcmp0 (op, "getvar") == 0) {
		const gchar *var = xb_node_get_attr (part, "var");
		g_autofree gchar *tmp = NULL;

		/* check required args */
		if (var == NULL) {
			tmp = xb_node_export (part, XB_NODE_EXPORT_FLAG_NONE, NULL);
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "required var for part: %s", tmp);
			return FALSE;
		}

		/* just has to be non-empty */
		if (!fu_fastboot_device_getvar (device, var, &tmp, error))
			return FALSE;
		if (tmp == NULL || tmp[0] == '\0') {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "failed to getvar %s", var);
			return FALSE;
		}
		return TRUE;
	}

	/* erase */
	if (g_strcmp0 (op, "erase") == 0) {
		const gchar *partition = xb_node_get_attr (part, "partition");
		g_autofree gchar *cmd = g_strdup_printf ("erase:%s", partition);

		/* check required args */
		if (partition == NULL) {
			g_autofree gchar *tmp = NULL;
			tmp = xb_node_export (part, XB_NODE_EXPORT_FLAG_NONE, NULL);
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "required partition for part: %s", tmp);
			return FALSE;
		}

		/* erase the partition */
		return fu_fastboot_device_cmd (device, cmd,
					       FU_FASTBOOT_DEVICE_READ_FLAG_NONE,
					       error);
	}

	/* flash */
	if (g_strcmp0 (op, "flash") == 0) {
		GBytes *data;
		const gchar *filename = xb_node_get_attr (part, "filename");
		const gchar *partition = xb_node_get_attr (part, "partition");
		struct {
			GChecksumType kind;
			const gchar *str;
		} csum_kinds[] = {
			{ G_CHECKSUM_MD5,	"MD5" },
			{ G_CHECKSUM_SHA1,	"SHA1" },
			{ 0, NULL }
		};

		/* check required args */
		if (partition == NULL || filename == NULL) {
			g_autofree gchar *tmp = NULL;
			tmp = xb_node_export (part, XB_NODE_EXPORT_FLAG_NONE, NULL);
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "required partition and filename: %s", tmp);
			return FALSE;
		}

		/* find filename */
		data = fu_archive_lookup_by_fn (archive, filename, error);
		if (data == NULL)
			return FALSE;

		/* checksum is optional */
		for (guint i = 0; csum_kinds[i].str != NULL; i++) {
			const gchar *csum;
			g_autofree gchar *csum_actual = NULL;

			/* not provided */
			csum = xb_node_get_attr (part, csum_kinds[i].str);
			if (csum == NULL)
				continue;

			/* check is valid */
			csum_actual = g_compute_checksum_for_bytes (csum_kinds[i].kind, data);
			if (g_strcmp0 (csum, csum_actual) != 0) {
				g_set_error (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "%s invalid, expected %s, got %s",
					     filename, csum, csum_actual);
				return FALSE;
			}
		}

		/* flash the partition */
		if (!fu_fastboot_device_download (device, data, error))
			return FALSE;
		return fu_fastboot_device_flash (device, partition, error);
	}

	/* dumb operation that doesn't expect a response */
	if (g_strcmp0 (op, "boot") == 0 ||
	    g_strcmp0 (op, "continue") == 0 ||
	    g_strcmp0 (op, "reboot") == 0 ||
	    g_strcmp0 (op, "reboot-bootloader") == 0 ||
	    g_strcmp0 (op, "powerdown") == 0) {
		return fu_fastboot_device_cmd (device, op,
					       FU_FASTBOOT_DEVICE_READ_FLAG_NONE,
					       error);
	}

	/* unknown */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_INVALID_DATA,
		     "unknown operation %s", op);
	return FALSE;
}

static gboolean
fu_fastboot_device_write_motorola (FuDevice *device,
				   FuArchive* archive,
				   GError **error)
{
	GBytes *data;
	g_autoptr(GPtrArray) parts = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();
	g_autoptr(XbSilo) silo = NULL;

	/* load the manifest of operations */
	data = fu_archive_lookup_by_fn (archive, "flashfile.xml", error);
	if (data == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes (source, data,
					   XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source (builder, source);
	silo = xb_builder_compile (builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	/* get all the operation parts */
	parts = xb_silo_query (silo, "parts/part", 0, error);
	if (parts == NULL)
		return FALSE;
	for (guint i = 0; i < parts->len; i++) {
		XbNode *part = g_ptr_array_index (parts, i);
		if (!fu_fastboot_device_write_motorola_part (device,
							     archive,
							     part,
							     error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fastboot_device_write_qfil (FuDevice *device, FuArchive* archive, GError **error)
{
	GBytes *data;
	g_autoptr(GPtrArray) parts = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();
	g_autoptr(XbSilo) silo = NULL;

	/* load the manifest of operations */
	data = fu_archive_lookup_by_fn (archive, "partition_nand.xml", error);
	if (data == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes (source, data,
					   XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source (builder, source);
	silo = xb_builder_compile (builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	/* get all the operation parts */
	parts = xb_silo_query (silo, "nandboot/partitions/partition", 0, error);
	if (parts == NULL)
		return FALSE;
	for (guint i = 0; i < parts->len; i++) {
		XbNode *part = g_ptr_array_index (parts, i);
		if (!fu_fastboot_device_write_qfil_part (device,
							 archive,
							 part,
							 error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fastboot_device_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* decompress entire archive ahead of time */
	archive = fu_archive_new (fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* load the manifest of operations */
	if (fu_archive_lookup_by_fn (archive, "partition_nand.xml", NULL) != NULL)
		return fu_fastboot_device_write_qfil (device, archive, error);
	if (fu_archive_lookup_by_fn (archive, "flashfile.xml", NULL) != NULL) {
		return fu_fastboot_device_write_motorola (device,
							  archive,
							  error);
	}

	/* not supported */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "manifest not supported");
	return FALSE;
}

static gboolean
fu_fastboot_device_close (FuUsbDevice *device, GError **error)
{
	FuFastbootDevice *self = FU_FASTBOOT_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* we're done here */
	if (!g_usb_device_release_interface (usb_device, self->intf_nr,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fastboot_device_set_quirk_kv (FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuFastbootDevice *self = FU_FASTBOOT_DEVICE (device);

	/* load slave address from quirks */
	if (g_strcmp0 (key, "FastbootBlockSize") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp >= 0x40 && tmp < 0x100000) {
			self->blocksz = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid block size");
		return FALSE;
	}

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;

}

static gboolean
fu_fastboot_device_attach (FuDevice *device, GError **error)
{
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_fastboot_device_cmd (device, "reboot",
				     FU_FASTBOOT_DEVICE_READ_FLAG_NONE,
				     error))
		return FALSE;
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_fastboot_device_init (FuFastbootDevice *self)
{
	/* this is a safe default, even using USBv1 */
	self->blocksz = 512;
	fu_device_set_protocol (FU_DEVICE (self), "com.google.fastboot");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_set_remove_delay (FU_DEVICE (self), FASTBOOT_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_fastboot_device_class_init (FuFastbootDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->probe = fu_fastboot_device_probe;
	klass_device->setup = fu_fastboot_device_setup;
	klass_device->write_firmware = fu_fastboot_device_write_firmware;
	klass_device->attach = fu_fastboot_device_attach;
	klass_device->to_string = fu_fastboot_device_to_string;
	klass_device->set_quirk_kv = fu_fastboot_device_set_quirk_kv;
	klass_usb_device->open = fu_fastboot_device_open;
	klass_usb_device->close = fu_fastboot_device_close;
}
