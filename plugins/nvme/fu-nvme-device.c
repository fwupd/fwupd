/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gstdio.h>

#include <efivar.h>
#include <linux/nvme_ioctl.h>

#include "fu-chunk.h"
#include "fu-nvme-device.h"

#define FU_NVME_ID_CTRL_SIZE	0x1000

struct _FuNvmeDevice {
	FuUdevDevice		 parent_instance;
	guint			 pci_depth;
	gint			 fd;
};

G_DEFINE_TYPE (FuNvmeDevice, fu_nvme_device, FU_TYPE_UDEV_DEVICE)

#ifndef HAVE_GUDEV_232
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
#pragma clang diagnostic pop
#endif

static void
fu_nvme_device_to_string (FuDevice *device, GString *str)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);
	g_string_append (str, "  FuNvmeDevice:\n");
	g_string_append_printf (str, "    fd:\t\t\t%i\n", self->fd);
	g_string_append_printf (str, "    pci-depth:\t\t%u\n", self->pci_depth);
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
	efi_guid_t guid_tmp;
	guint guint_sum = 0;
	g_autofree char *guid = NULL;

	/* check the GUID is plausible */
	for (guint i = 0; i < 16; i++)
		guint_sum += buf[addr_start + i];
	if (guint_sum == 0x00)
		return NULL;
	if (guint_sum < 0xff) {
		g_warning ("implausible GUID with sum %02x", guint_sum);
		return NULL;
	}
	memcpy (&guid_tmp, buf + addr_start, 16);
	if (efi_guid_to_str (&guid_tmp, &guid) < 0)
		return NULL;
	return g_strdup (guid);
}

static gboolean
fu_nvme_device_submit_admin_passthru (FuNvmeDevice *self, struct nvme_admin_cmd *cmd, GError **error)
{
	if (ioctl (self->fd, NVME_IOCTL_ADMIN_CMD, cmd) < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to issue admin command 0x%02x: %s",
			     cmd->opcode,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
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
		.cdw10		= (data_sz >> 2) - 1,
		.cdw11		= addr >> 2,
	};
	memcpy (&cmd.addr, &data, sizeof (gpointer));
	return fu_nvme_device_submit_admin_passthru (self, &cmd, error);
}

static void
fu_nvme_device_parse_cns_maybe_dell (FuNvmeDevice *self, const guint8 *buf)
{
	g_autofree gchar *component_id = NULL;
	g_autofree gchar *guid_efi = NULL;
	g_autofree gchar *guid_id = NULL;

	/* add extra component ID if set */
	component_id = fu_nvme_device_get_string_safe (buf, 0xc36, 0xc3d);
	if (component_id == NULL || strlen (component_id) < 4) {
		g_debug ("invalid component ID, skipping");
		return;
	}
	guid_id = g_strdup_printf ("STORAGE-DELL-%s", component_id);
	fu_device_add_guid (FU_DEVICE (self), guid_id);

	/* also add the EFI GUID */
	guid_efi = fu_nvme_device_get_guid_safe (buf, 0x0c26);
	if (guid_efi != NULL)
		fu_device_add_guid (FU_DEVICE (self), guid_efi);
}

static gboolean
fu_nvme_device_parse_cns (FuNvmeDevice *self, const guint8 *buf, gsize sz, GError **error)
{
	guint8 fawr;
	guint8 nfws;
	guint8 s1ro;
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
	if (sr != NULL)
		fu_device_set_version (FU_DEVICE (self), sr);

	/* firmware slot information */
	fawr = (buf[260] & 0x10) >> 4;
	nfws = (buf[260] & 0x0e) >> 1;
	s1ro = buf[260] & 0x01;
	g_debug ("nr fw slots: %u, slot1 r/o: %u", nfws, s1ro);
	if (!fawr)
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);

	/* Dell helpfully provide an EFI GUID we can use in the vendor offset,
	 * but don't have a header or any magic we can use -- so check if the
	 * component ID looks plausible and the GUID is "sane" */
	fu_nvme_device_parse_cns_maybe_dell (self, buf);

	/* fall back to the device description */
	if (fu_device_get_guids (FU_DEVICE (self))->len == 0) {
		g_debug ("no vendor GUID, falling back to mn");
		fu_device_add_guid (FU_DEVICE (self), mn);
	}
	return TRUE;
}

static guint
fu_nvme_device_pci_slot_depth (FuNvmeDevice *self)
{
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (self));
	g_autoptr(GUdevDevice) device_tmp = NULL;

	device_tmp = g_udev_device_get_parent_with_subsystem (udev_device, "pci", NULL);
	if (device_tmp == NULL)
		return 0;
	for (guint i = 0; i < 0xff; i++) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent (device_tmp);
		if (parent == NULL)
			return i;
		g_set_object (&device_tmp, parent);
	}
	return 0;
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
fu_nvme_device_open (FuDevice *device, GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (device));
	guint8 buf[FU_NVME_ID_CTRL_SIZE];

	/* open device */
	self->fd = g_open (g_udev_device_get_device_file (udev_device), O_RDONLY);
	if (self->fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to open %s: %s",
			     fu_device_get_platform_id (FU_DEVICE (self)),
			     strerror (errno));
		return FALSE;
	}

	/* look at the PCI depth to work out if in an external enclosure */
	self->pci_depth = fu_nvme_device_pci_slot_depth (self);
	if (self->pci_depth <= 2)
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);

	/* get and parse CNS */
	if (!fu_nvme_device_identify_ctrl (self, buf, error)) {
		g_prefix_error (error, "failed to identify %s: ",
				fu_device_get_platform_id (FU_DEVICE (self)));
		return FALSE;
	}
	fu_nvme_device_dump ("CNS", buf, sizeof (buf));
	if (!fu_nvme_device_parse_cns (self, buf, sizeof(buf), error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_nvme_device_close (FuDevice *device, GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);
	if (!g_close (self->fd, error))
		return FALSE;
	self->fd = 0;
	return TRUE;
}

static gboolean
fu_nvme_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE (device);
	g_autoptr(GPtrArray) chunks = NULL;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,	/* start_addr */
						0x00,	/* page_sz */
						0x1000);/* block size */

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

static void
fu_nvme_device_init (FuNvmeDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_summary (FU_DEVICE (self), "NVM Express Solid State Drive");
	fu_device_add_icon (FU_DEVICE (self), "drive-harddisk");
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
	object_class->finalize = fu_nvme_device_finalize;
	klass_device->to_string = fu_nvme_device_to_string;
	klass_device->open = fu_nvme_device_open;
	klass_device->close = fu_nvme_device_close;
	klass_device->write_firmware = fu_nvme_device_write_firmware;
}

FuNvmeDevice *
fu_nvme_device_new (GUdevDevice *udev_device)
{
	FuNvmeDevice *self;
	self = g_object_new (FU_TYPE_NVME_DEVICE,
			     "udev-device", udev_device,
			     NULL);
	return self;
}

FuNvmeDevice *
fu_nvme_device_new_from_blob (const guint8 *buf, gsize sz, GError **error)
{
	g_autoptr(FuNvmeDevice) self = g_object_new (FU_TYPE_NVME_DEVICE, NULL);
	if (!fu_nvme_device_parse_cns (self, buf, sz, error))
		return NULL;
	return g_steal_pointer (&self);
}
