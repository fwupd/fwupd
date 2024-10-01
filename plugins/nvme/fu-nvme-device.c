/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/nvme_ioctl.h>
#include <sys/ioctl.h>

#include "fu-nvme-common.h"
#include "fu-nvme-device.h"

#define FU_NVME_ID_CTRL_SIZE 0x1000

struct _FuNvmeDevice {
	FuPciDevice parent_instance;
	guint pci_depth;
	guint64 write_block_size;
};

#define FU_NVME_COMMIT_ACTION_CA0 0b000 /* replace only */
#define FU_NVME_COMMIT_ACTION_CA1 0b001 /* replace, and activate on next reset */
#define FU_NVME_COMMIT_ACTION_CA2 0b010 /* activate on next reset */
#define FU_NVME_COMMIT_ACTION_CA3 0b011 /* replace, and activate immediately */

#define FU_NVME_DEVICE_FLAG_FORCE_ALIGN "force-align"
#define FU_NVME_DEVICE_FLAG_COMMIT_CA3	"commit-ca3"

G_DEFINE_TYPE(FuNvmeDevice, fu_nvme_device, FU_TYPE_PCI_DEVICE)

#define FU_NVME_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

static void
fu_nvme_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuNvmeDevice *self = FU_NVME_DEVICE(device);
	fwupd_codec_string_append_int(str, idt, "PciDepth", self->pci_depth);
}

/* @addr_start and @addr_end are *inclusive* to match the NMVe specification */
static gchar *
fu_nvme_device_get_string_safe(const guint8 *buf, guint16 addr_start, guint16 addr_end)
{
	GString *str;

	g_return_val_if_fail(buf != NULL, NULL);
	g_return_val_if_fail(addr_start < addr_end, NULL);

	str = g_string_new_len(NULL, addr_end + addr_start + 1);
	for (guint16 i = addr_start; i <= addr_end; i++) {
		gchar tmp = (gchar)buf[i];
		/* skip leading spaces */
		if (g_ascii_isspace(tmp) && str->len == 0)
			continue;
		if (g_ascii_isprint(tmp))
			g_string_append_c(str, tmp);
	}

	/* nothing found */
	if (str->len == 0) {
		g_string_free(str, TRUE);
		return NULL;
	}
	return g_strchomp(g_string_free(str, FALSE));
}

static gchar *
fu_nvme_device_get_guid_safe(const guint8 *buf, guint16 addr_start)
{
	if (!fu_common_guid_is_plausible(buf + addr_start))
		return NULL;
	return fwupd_guid_to_string((const fwupd_guid_t *)(buf + addr_start),
				    FWUPD_GUID_FLAG_MIXED_ENDIAN);
}

static gboolean
fu_nvme_device_submit_admin_passthru(FuNvmeDevice *self, struct nvme_admin_cmd *cmd, GError **error)
{
	gint rc = 0;
	guint32 err;

	/* submit admin command */
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  NVME_IOCTL_ADMIN_CMD,
				  (guint8 *)cmd,
				  sizeof(*cmd),
				  &rc,
				  FU_NVME_DEVICE_IOCTL_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				  error)) {
		g_prefix_error(error, "failed to issue admin command 0x%02x: ", cmd->opcode);
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
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Not supported: %s",
		    fu_nvme_status_to_string(err));
	return FALSE;
}

static gboolean
fu_nvme_device_identify_ctrl(FuNvmeDevice *self, guint8 *data, GError **error)
{
	struct nvme_admin_cmd cmd = {
	    .opcode = 0x06,
	    .nsid = 0x00,
	    .addr = 0x0, /* memory address of data */
	    .data_len = FU_NVME_ID_CTRL_SIZE,
	    .cdw10 = 0x01,
	    .cdw11 = 0x00,
	};
	memcpy(&cmd.addr, &data, sizeof(gpointer)); /* nocheck:blocked */
	return fu_nvme_device_submit_admin_passthru(self, &cmd, error);
}

static gboolean
fu_nvme_device_fw_commit(FuNvmeDevice *self,
			 guint8 slot,
			 guint8 action,
			 guint8 bpid,
			 GError **error)
{
	struct nvme_admin_cmd cmd = {
	    .opcode = 0x10,
	    .cdw10 = (bpid << 31) | (action << 3) | slot,
	};
	return fu_nvme_device_submit_admin_passthru(self, &cmd, error);
}

static gboolean
fu_nvme_device_fw_download(FuNvmeDevice *self,
			   guint32 addr,
			   const guint8 *data,
			   guint32 data_sz,
			   GError **error)
{
	struct nvme_admin_cmd cmd = {
	    .opcode = 0x11,
	    .addr = 0x0, /* memory address of data */
	    .data_len = data_sz,
	    .cdw10 = (data_sz >> 2) - 1, /* convert to DWORDs */
	    .cdw11 = addr >> 2,		 /* convert to DWORDs */
	};
	memcpy(&cmd.addr, &data, sizeof(gpointer)); /* nocheck:blocked */
	return fu_nvme_device_submit_admin_passthru(self, &cmd, error);
}

static void
fu_nvme_device_parse_cns_maybe_dell(FuNvmeDevice *self, const guint8 *buf)
{
	g_autofree gchar *component_id = NULL;
	g_autofree gchar *devid = NULL;
	g_autofree gchar *guid_efi = NULL;
	g_autofree gchar *guid = NULL;

	/* add extra component ID if set */
	component_id = fu_nvme_device_get_string_safe(buf, 0xc36, 0xc3d);
	if (component_id == NULL || !g_str_is_ascii(component_id) || strlen(component_id) < 6) {
		g_debug("invalid component ID, skipping");
		return;
	}

	/* do not add the FuUdevDevice instance IDs as generic firmware
	 * should not be used on these OEM-specific devices */
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_AUTO_INSTANCE_IDS);

	/* add instance ID *and* GUID as using no-auto-instance-ids */
	devid = g_strdup_printf("STORAGE-DELL-%s", component_id);
	fu_device_add_instance_id(FU_DEVICE(self), devid);
	guid = fwupd_guid_hash_string(devid);
	fu_device_add_guid(FU_DEVICE(self), guid);

	/* also add the EFI GUID */
	guid_efi = fu_nvme_device_get_guid_safe(buf, 0x0c26);
	if (guid_efi != NULL)
		fu_device_add_guid(FU_DEVICE(self), guid_efi);
}

static gboolean
fu_nvme_device_parse_cns(FuNvmeDevice *self, const guint8 *buf, gsize sz, GError **error)
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
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to parse blob, expected 0x%04x bytes",
			    (guint)FU_NVME_ID_CTRL_SIZE);
		return FALSE;
	}

	/* get sanitized string from CNS -- see the following doc for offsets:
	 * NVM-Express-1_3c-2018.05.24-Ratified.pdf */
	sn = fu_nvme_device_get_string_safe(buf, 4, 23);
	if (sn != NULL)
		fu_device_set_serial(FU_DEVICE(self), sn);
	mn = fu_nvme_device_get_string_safe(buf, 24, 63);
	if (mn != NULL)
		fu_device_set_name(FU_DEVICE(self), mn);
	sr = fu_nvme_device_get_string_safe(buf, 64, 71);
	if (sr != NULL)
		fu_device_set_version(FU_DEVICE(self), sr);

	/* firmware update granularity (FWUG) */
	fwug = buf[319];
	if (fwug != 0x00 && fwug != 0xff)
		self->write_block_size = ((guint64)fwug) * 0x1000;

	/* firmware slot information */
	fawr = (buf[260] & 0x10) >> 4;
	nfws = (buf[260] & 0x0e) >> 1;
	s1ro = buf[260] & 0x01;
	g_debug("fawr: %u, nr fw slots: %u, slot1 r/o: %u", fawr, nfws, s1ro);

	/* FRU globally unique identifier (FGUID) */
	gu = fu_nvme_device_get_guid_safe(buf, 127);
	if (gu != NULL)
		fu_device_add_guid(FU_DEVICE(self), gu);

	/* Dell helpfully provide an EFI GUID we can use in the vendor offset,
	 * but don't have a header or any magic we can use -- so check if the
	 * component ID looks plausible and the GUID is "sane" */
	fu_nvme_device_parse_cns_maybe_dell(self, buf);

	/* fall back to the device description */
	if (fu_device_get_guids(FU_DEVICE(self))->len == 0) {
		g_debug("no vendor GUID, falling back to mn");
		fu_device_add_instance_id(FU_DEVICE(self), mn);
	}
	return TRUE;
}

static gboolean
fu_nvme_device_pci_probe(FuNvmeDevice *self, GError **error)
{
	g_autoptr(FuDevice) pci_donor = NULL;

	pci_donor = fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "pci", error);
	if (pci_donor == NULL)
		return FALSE;
	if (!fu_device_probe(pci_donor, error))
		return FALSE;
	fu_device_add_instance_str(FU_DEVICE(self),
				   "VEN",
				   fu_device_get_instance_str(pci_donor, "VEN"));
	fu_device_add_instance_str(FU_DEVICE(self),
				   "DEV",
				   fu_device_get_instance_str(pci_donor, "DEV"));
	fu_device_add_instance_str(FU_DEVICE(self),
				   "SUBSYS",
				   fu_device_get_instance_str(pci_donor, "SUBSYS"));
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "NVME", "VEN", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "NVME",
					      "VEN",
					      NULL))
		return FALSE;
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "NVME", "VEN", "DEV", "SUBSYS", NULL);
	fu_pci_device_set_revision(FU_PCI_DEVICE(self),
				   fu_pci_device_get_revision(FU_PCI_DEVICE(pci_donor)));
	fu_device_incorporate(FU_DEVICE(self),
			      pci_donor,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR |
				  FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID |
				  FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);

	/* success */
	return TRUE;
}

static gboolean
fu_nvme_device_probe(FuDevice *device, GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE(device);

	/* copy the PCI-specific instance parts and make them NVME for GUID compat */
	if (!fu_nvme_device_pci_probe(self, error))
		return FALSE;

	/* fix up vendor name so we can remove it from the product name */
	if (g_strcmp0(fu_device_get_vendor(FU_DEVICE(device)), "Samsung Electronics Co Ltd") == 0)
		fu_device_set_vendor(FU_DEVICE(device), "Samsung");

	/* look at the PCI depth to work out if in an external enclosure */
	self->pci_depth = fu_udev_device_get_subsystem_depth(FU_UDEV_DEVICE(device), "pci");
	if (self->pci_depth <= 2) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	}

	/* most devices need at least a warm reset, but some quirked drives
	 * need a full "cold" shutdown and startup */
	if (!fu_device_has_private_flag(device, FU_NVME_DEVICE_FLAG_COMMIT_CA3) &&
	    !fu_device_has_flag(self, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN))
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);

	return TRUE;
}

static gboolean
fu_nvme_device_setup(FuDevice *device, GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE(device);
	guint8 buf[FU_NVME_ID_CTRL_SIZE] = {0x0};

	/* get and parse CNS */
	if (!fu_nvme_device_identify_ctrl(self, buf, error)) {
		g_prefix_error(error,
			       "failed to identify %s: ",
			       fu_device_get_physical_id(FU_DEVICE(self)));
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "CNS", buf, sizeof(buf));
	if (!fu_nvme_device_parse_cns(self, buf, sizeof(buf), error))
		return FALSE;

	/* add one extra instance ID so that we can match bad firmware */
	fu_device_add_instance_strsafe(device, "VER", fu_device_get_version(device));
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "NVME",
					 "VEN",
					 "DEV",
					 "VER",
					 NULL);

	/* success */
	return TRUE;
}

static gboolean
fu_nvme_device_write_firmware(FuDevice *device,
			      FuFirmware *firmware,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE(device);
	g_autoptr(GBytes) fw2 = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	guint64 block_size = self->write_block_size > 0 ? self->write_block_size : 0x1000;
	guint8 commit_action = FU_NVME_COMMIT_ACTION_CA1;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 80, "commit");

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* some vendors provide firmware files whose sizes are not multiples
	 * of blksz *and* the device won't accept blocks of different sizes */
	if (fu_device_has_private_flag(device, FU_NVME_DEVICE_FLAG_FORCE_ALIGN)) {
		fw2 = fu_bytes_align(fw, block_size, 0xff);
	} else {
		fw2 = g_bytes_ref(fw);
	}

	/* write each block */
	chunks = fu_chunk_array_new_from_bytes(fw2, 0x00, block_size);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_nvme_device_fw_download(self,
						fu_chunk_get_address(chk),
						fu_chunk_get_data(chk),
						fu_chunk_get_data_sz(chk),
						error)) {
			g_prefix_error(error, "failed to write chunk %u: ", i);
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* commit */
	if (fu_device_has_private_flag(device, FU_NVME_DEVICE_FLAG_COMMIT_CA3))
		commit_action = FU_NVME_COMMIT_ACTION_CA3;
	if (!fu_nvme_device_fw_commit(self,
				      0x00, /* let controller choose */
				      commit_action,
				      0x00, /* boot partition identifier */
				      error)) {
		g_prefix_error(error, "failed to commit to auto slot: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_nvme_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuNvmeDevice *self = FU_NVME_DEVICE(device);
	if (g_strcmp0(key, "NvmeBlockSize") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->write_block_size = tmp;
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_nvme_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 20, "reload");
}

static void
fu_nvme_device_init(FuNvmeDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_summary(FU_DEVICE(self), "NVM Express solid state drive");
	fu_device_add_icon(FU_DEVICE(self), "drive-harddisk");
	fu_device_add_protocol(FU_DEVICE(self), "org.nvmexpress");
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_device_register_private_flag(FU_DEVICE(self), FU_NVME_DEVICE_FLAG_FORCE_ALIGN);
	fu_device_register_private_flag(FU_DEVICE(self), FU_NVME_DEVICE_FLAG_COMMIT_CA3);
}

static void
fu_nvme_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_nvme_device_parent_class)->finalize(object);
}

static void
fu_nvme_device_class_init(FuNvmeDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_nvme_device_finalize;
	device_class->to_string = fu_nvme_device_to_string;
	device_class->set_quirk_kv = fu_nvme_device_set_quirk_kv;
	device_class->setup = fu_nvme_device_setup;
	device_class->write_firmware = fu_nvme_device_write_firmware;
	device_class->probe = fu_nvme_device_probe;
	device_class->set_progress = fu_nvme_device_set_progress;
}

FuNvmeDevice *
fu_nvme_device_new_from_blob(FuContext *ctx, const guint8 *buf, gsize sz, GError **error)
{
	g_autoptr(FuNvmeDevice) self = NULL;
	self = g_object_new(FU_TYPE_NVME_DEVICE, "context", ctx, NULL);
	if (!fu_nvme_device_parse_cns(self, buf, sz, error))
		return NULL;
	return g_steal_pointer(&self);
}
