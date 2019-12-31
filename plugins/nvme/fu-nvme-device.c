/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>

#include "fu-chunk.h"
#include "fu-nvme-common.h"
#include "fu-nvme-device.h"

#define FU_NVME_ID_CTRL_SIZE	0x1000

struct _FuNvmeDevice {
	FuUdevDevice		 parent_instance;
	guint			 pci_depth;
	guint64			 write_block_size;
};

G_DEFINE_TYPE (FuNvmeDevice, fu_nvme_device, FU_TYPE_UDEV_DEVICE)

static void
fu_nvme_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);
	fu_common_string_append_ku (str, idt, "PciDepth", self->pci_depth);
}

/* @addr_start and @addr_end are *inclusive* to match the NMVe specification */
static gchar *
fu_nvme_device_get_string_safe (const guint8 *buf, guint16 addr_start, guint16 addr_end)
{
	GString *str;

	g_return_val_if_fail (buf != NULL, NULL);
	g_return_val_if_fail (addr_start < addr_end, NULL);

	str = g_string_new_len (NULL, addr_end + addr_start + 1);
	for (guint16 i = addr_start; i <= addr_end; i++) {
		gchar tmp = (gchar) buf[i];
		/* skip leading spaces */
		if (g_ascii_isspace (tmp) && str->len == 0)
			continue;
		if (g_ascii_isprint (tmp))
			g_string_append_c (str, tmp);
	}

	/* nothing found */
	if (str->len == 0) {
		g_string_free (str, TRUE);
		return NULL;
	}
	return g_strchomp (g_string_free (str, FALSE));
}

static gchar *
fu_nvme_device_get_guid_safe (const guint8 *buf, guint16 addr_start)
{
	if (!fu_common_guid_is_plausible (buf + addr_start))
		return NULL;
	return fwupd_guid_to_string ((const fwupd_guid_t *) (buf + addr_start),
				     FWUPD_GUID_FLAG_MIXED_ENDIAN);
}

static gboolean
fu_nvme_device_submit_admin_passthru (FuNvmeDevice *self, struct nvme_admin_cmd *cmd, GError **error)
{
	gint rc = 0;
	guint32 err;

	/* submit admin command */
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   NVME_IOCTL_ADMIN_CMD, (guint8 *) cmd,
				   &rc, error)) {
		g_prefix_error (error,
				"failed to issue admin command 0x%02x: ",
				cmd->opcode);
		return FALSE;
	}

	/* check the error code */
	err = rc & 0x3ff;
	switch (err) {
	case NVME_SC_SUCCESS:
	/* devices are always added with _NEEDS_REBOOT, so ignore */
	case NVME_SC_FW_NEEDS_CONV_RESET:
	case NVME_SC_FW_NEEDS_SUBSYS_RESET:
	case NVME_SC_FW_NEEDS_RESET:
		return TRUE;
	default:
		break;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "Not supported: %s",
		     fu_nvme_status_to_string (err));
	return FALSE;

}

static gboolean
fu_nvme_device_identify_ctrl (FuNvmeDevice *self, guint8 *data, GError **error)
{
	struct nvme_admin_cmd cmd = {
		.opcode		= 0x06,
		.nsid		= 0x00,
		.addr		= 0x0, /* memory address of data */
		.data_len	= FU_NVME_ID_CTRL_SIZE,
		.cdw10		= 0x01,
		.cdw11		= 0x00,
	};
	memcpy (&cmd.addr, &data, sizeof (gpointer));
	return fu_nvme_device_submit_admin_passthru (self, &cmd, error);
}

static gboolean
fu_nvme_device_fw_commit (FuNvmeDevice *self,
			  guint8 slot,
			  guint8 action,
			  guint8 bpid,
			  GError **error)
{
	struct nvme_admin_cmd cmd = {
		.opcode		= 0x10,
		.cdw10		= (bpid << 31) | (action << 3) | slot,
	};
	return fu_nvme_device_submit_admin_passthru (self, &cmd, error);
}

static gboolean
fu_nvme_device_fw_download (FuNvmeDevice *self,
			    guint32 addr,
			    const guint8 *data,
			    guint32 data_sz,
			    GError **error)
{
	struct nvme_admin_cmd cmd = {
		.opcode		= 0x11,
		.addr		= 0x0, /* memory address of data */
		.data_len	= data_sz,
		.cdw10		= (data_sz >> 2) - 1,	/* convert to DWORDs */
		.cdw11		= addr >> 2,		/* convert to DWORDs */
	};
	memcpy (&cmd.addr, &data, sizeof (gpointer));
	return fu_nvme_device_submit_admin_passthru (self, &cmd, error);
}

static void
fu_nvme_device_parse_cns_maybe_dell (FuNvmeDevice *self, const guint8 *buf)
{
	g_autofree gchar *component_id = NULL;
	g_autofree gchar *devid = NULL;
	g_autofree gchar *guid_efi = NULL;
	g_autofree gchar *guid = NULL;

	/* add extra component ID if set */
	component_id = fu_nvme_device_get_string_safe (buf, 0xc36, 0xc3d);
	if (component_id == NULL ||
	   !g_str_is_ascii (component_id) ||
	    strlen (component_id) < 6) {
		g_debug ("invalid component ID, skipping");
		return;
	}

	/* do not add the FuUdevDevice instance IDs as generic firmware
	 * should not be used on these OEM-specific devices */
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS);

	/* add instance ID *and* GUID as using no-auto-instance-ids */
	devid = g_strdup_printf ("STORAGE-DELL-%s", component_id);
	fu_device_add_instance_id (FU_DEVICE (self), devid);
	guid = fwupd_guid_hash_string (devid);
	fu_device_add_guid (FU_DEVICE (self), guid);

	/* also add the EFI GUID */
	guid_efi = fu_nvme_device_get_guid_safe (buf, 0x0c26);
	if (guid_efi != NULL)
		fu_device_add_guid (FU_DEVICE (self), guid_efi);
}

static gboolean
fu_nvme_device_set_version (FuNvmeDevice *self, const gchar *version, GError **error)
{
	FwupdVersionFormat fmt = fu_device_get_version_format (FU_DEVICE (self));

	/* unset */
	if (fmt == FWUPD_VERSION_FORMAT_UNKNOWN || fmt == FWUPD_VERSION_FORMAT_PLAIN) {
		fu_device_set_version (FU_DEVICE (self), version, FWUPD_VERSION_FORMAT_PLAIN);
		return TRUE;
	}

	/* AA.BB.CC.DD */
	if (fmt == FWUPD_VERSION_FORMAT_QUAD) {
		guint64 tmp = g_ascii_strtoull (version, NULL, 16);
		g_autofree gchar *version_new = NULL;
		if (tmp == 0 || tmp > G_MAXUINT32) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "%s is not valid 32 bit number",
				     version);
			return FALSE;
		}
		version_new = fu_common_version_from_uint32 (tmp, FWUPD_VERSION_FORMAT_QUAD);
		fu_device_set_version (FU_DEVICE (self), version_new, fmt);
		return TRUE;
	}

	/* invalid, or not supported */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_INVALID_DATA,
		     "version format %s not handled",
		     fwupd_version_format_to_string (fmt));
	return FALSE;
}

static gboolean
fu_nvme_device_parse_cns (FuNvmeDevice *self, const guint8 *buf, gsize sz, GError **error)
{
	guint8 fawr;
	guint8 fwug;
	guint8 nfws;
	guint8 s1ro;
	g_autofree gchar *gu = NULL;
	g_autofree gchar *mn = NULL;
	g_autofree gchar *sn = NULL;
	g_autofree gchar *sr = NULL;

	/* wrong size */
	if (sz != FU_NVME_ID_CTRL_SIZE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to parse blob, expected 0x%04x bytes",
			     (guint) FU_NVME_ID_CTRL_SIZE);
		return FALSE;
	}

	/* get sanitiezed string from CNS -- see the following doc for offsets:
	 * NVM-Express-1_3c-2018.05.24-Ratified.pdf */
	sn = fu_nvme_device_get_string_safe (buf, 4, 23);
	if (sn != NULL)
		fu_device_set_serial (FU_DEVICE (self), sn);
	mn = fu_nvme_device_get_string_safe (buf, 24, 63);
	if (mn != NULL)
		fu_device_set_name (FU_DEVICE (self), mn);
	sr = fu_nvme_device_get_string_safe (buf, 64, 71);
	if (sr != NULL) {
		if (!fu_nvme_device_set_version (self, sr, error))
			return FALSE;
	}

	/* firmware update granularity (FWUG) */
	fwug = buf[319];
	if (fwug != 0x00 && fwug != 0xff)
		self->write_block_size = ((guint64) fwug) * 0x1000;

	/* firmware slot information */
	fawr = (buf[260] & 0x10) >> 4;
	nfws = (buf[260] & 0x0e) >> 1;
	s1ro = buf[260] & 0x01;
	g_debug ("fawr: %u, nr fw slots: %u, slot1 r/o: %u", fawr, nfws, s1ro);

	/* FRU globally unique identifier (FGUID) */
	gu = fu_nvme_device_get_guid_safe (buf, 127);
	if (gu != NULL)
		fu_device_add_guid (FU_DEVICE (self), gu);

	/* Dell helpfully provide an EFI GUID we can use in the vendor offset,
	 * but don't have a header or any magic we can use -- so check if the
	 * component ID looks plausible and the GUID is "sane" */
	fu_nvme_device_parse_cns_maybe_dell (self, buf);

	/* fall back to the device description */
	if (fu_device_get_guids (FU_DEVICE (self))->len == 0) {
		g_debug ("no vendor GUID, falling back to mn");
		fu_device_add_instance_id (FU_DEVICE (self), mn);
	}
	return TRUE;
}

static void
fu_nvme_device_dump (const gchar *title, const guint8 *buf, gsize sz)
{
	if (g_getenv ("FWPUD_NVME_VERBOSE") == NULL)
		return;
	g_print ("%s (%" G_GSIZE_FORMAT "):", title, sz);
	for (gsize i = 0; i < sz; i++) {
		if (i % 64 == 0)
			g_print ("\naddr 0x%04x: ", (guint) i);
		g_print ("%02x", buf[i]);
	}
	g_print ("\n");
}

static gboolean
fu_nvme_device_probe (FuUdevDevice *device, GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id (device, "pci", error))
		return FALSE;

	/* look at the PCI depth to work out if in an external enclosure */
	self->pci_depth = fu_udev_device_get_slot_depth (device, "pci");
	if (self->pci_depth <= 2) {
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	}

	/* all devices need at least a warm reset, but some quirked drives
	 * need a full "cold" shutdown and startup */
	if (!fu_device_has_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN))
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);

	return TRUE;
}

static gboolean
fu_nvme_device_setup (FuDevice *device, GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);
	guint8 buf[FU_NVME_ID_CTRL_SIZE] = { 0x0 };

	/* get and parse CNS */
	if (!fu_nvme_device_identify_ctrl (self, buf, error)) {
		g_prefix_error (error, "failed to identify %s: ",
				fu_device_get_physical_id (FU_DEVICE (self)));
		return FALSE;
	}
	fu_nvme_device_dump ("CNS", buf, sizeof (buf));
	if (!fu_nvme_device_parse_cns (self, buf, sizeof(buf), error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_nvme_device_write_firmware (FuDevice *device,
			       FuFirmware *firmware,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);
	g_autoptr(GBytes) fw2 = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	guint64 block_size = self->write_block_size > 0 ?
			     self->write_block_size : 0x1000;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* some vendors provide firmware files whose sizes are not multiples
	 * of blksz *and* the device won't accept blocks of different sizes */
	if (fu_device_has_custom_flag (device, "force-align")) {
		fw2 = fu_common_bytes_align (fw, block_size, 0xff);
	} else {
		fw2 = g_bytes_ref (fw);
	}

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw2,
						0x00,		/* start_addr */
						0x00,		/* page_sz */
						block_size);	/* block size */

	/* write each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_nvme_device_fw_download (self,
						 chk->address,
						 chk->data,
						 chk->data_sz,
						 error)) {
			g_prefix_error (error, "failed to write chunk %u: ", i);
			return FALSE;
		}
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len + 1);
	}

	/* commit */
	if (!fu_nvme_device_fw_commit (self,
				       0x00,	/* let controller choose */
				       0x01,	/* download replaces, activated on reboot */
				       0x00,	/* boot partition identifier */
				       error)) {
		g_prefix_error (error, "failed to commit to auto slot: ");
		return FALSE;
	}

	/* success! */
	fu_device_set_progress (device, 100);
	return TRUE;
}

static gboolean
fu_nvme_device_set_quirk_kv (FuDevice *device,
			     const gchar *key,
			     const gchar *value,
			     GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);
	if (g_strcmp0 (key, "NvmeBlockSize") == 0) {
		self->write_block_size = fu_common_strtoull (value);
		return TRUE;
	}

	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_nvme_device_init (FuNvmeDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_summary (FU_DEVICE (self), "NVM Express Solid State Drive");
	fu_device_add_icon (FU_DEVICE (self), "drive-harddisk");
	fu_device_set_protocol (FU_DEVICE (self), "org.nvmexpress");
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
				  FU_UDEV_DEVICE_FLAG_OPEN_READ |
				  FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static void
fu_nvme_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_nvme_device_parent_class)->finalize (object);
}

static void
fu_nvme_device_class_init (FuNvmeDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_udev_device = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_nvme_device_finalize;
	klass_device->to_string = fu_nvme_device_to_string;
	klass_device->set_quirk_kv = fu_nvme_device_set_quirk_kv;
	klass_device->setup = fu_nvme_device_setup;
	klass_device->write_firmware = fu_nvme_device_write_firmware;
	klass_udev_device->probe = fu_nvme_device_probe;
}

FuNvmeDevice *
fu_nvme_device_new_from_blob (const guint8 *buf, gsize sz, GError **error)
{
	g_autoptr(FuNvmeDevice) self = g_object_new (FU_TYPE_NVME_DEVICE, NULL);
	if (!fu_nvme_device_parse_cns (self, buf, sz, error))
		return NULL;
	return g_steal_pointer (&self);
}
