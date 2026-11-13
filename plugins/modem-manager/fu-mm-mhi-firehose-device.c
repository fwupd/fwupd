/*
 * Copyright 2026 HongXing Xu <qifeng.liu@rollingwireless.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-mhi-firehose-device.h"

#define FU_MM_MHI_FIREHOSE_DEV_IMAGE_MAPPING_ID \
	"DEV_OTA_PACKAGE/WwanDeviceIdImageMappingTable.xml"
#define FU_MM_MHI_FIREHOSE_DEV_PARTITION_LABEL "f_dev_pack"

struct _FuMmMhiFirehoseDevice {
	FuMmDevice parent_instance;
	FuKernelSearchPathLocker *search_path_locker;
	GBytes *firehose_prog;
	gchar *firehose_prog_file;
	gchar *lib_firmware_path;
	gchar *wwan_device_id;
};

G_DEFINE_TYPE(FuMmMhiFirehoseDevice, fu_mm_mhi_firehose_device, FU_TYPE_MM_DEVICE)

static gboolean
fu_mm_mhi_firehose_resolve_dev_file(GBytes *mapping_blob,
				     const gchar * const *product_ids,
				     gchar **dev_file_out,
				     GError **error)
{
	const gchar *config_id = NULL;
	const gchar *dev_file = NULL;
	const gchar *matched_product_id = NULL;
	g_autoptr(GPtrArray) configs = NULL;
	g_autoptr(GPtrArray) skus = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	if (!xb_builder_source_load_bytes(source, mapping_blob, XB_BUILDER_SOURCE_FLAG_NONE, error)) {
		fwupd_error_convert(error);
		return FALSE;
	}
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}

	/* table 1: machine ProductID -> WwanDeviceConfigID */
	skus = xb_silo_query(silo,
			     "TableList/WwanDeviceConfigureIDTable/"
			     "UniqueSkuIDToWwanDeviceConfigureIDMappingTable/UniqueSkuID",
			     0,
			     NULL);
	if (skus == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no machine mappings in DEV image table");
		return FALSE;
	}
	for (guint i = 0; product_ids[i] != NULL; i++) {
		for (guint j = 0; j < skus->len; j++) {
			XbNode *sku = g_ptr_array_index(skus, j);
			const gchar *config_id_tmp = xb_node_get_attr(sku, "WwanDeviceConfigID");

			if (g_strcmp0(xb_node_get_attr(sku, "ProductID"), product_ids[i]) != 0)
				continue;
			if (config_id == NULL) {
				config_id = config_id_tmp;
				matched_product_id = product_ids[i];
			} else if (g_strcmp0(config_id, config_id_tmp) != 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "machine model maps to multiple DEV configurations");
				return FALSE;
			}
		}
	}
	if (config_id == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "current machine model is not in DEV image table");
		return FALSE;
	}

	/* table 2: WwanDeviceConfigID -> DEV image directory */
	configs = xb_silo_query(silo,
				"TableList/WwanDeviceConfigureIDToImageMappingTable/"
				"WwanDeviceConfigID",
				0,
				NULL);
	if (configs == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no DEV image configurations");
		return FALSE;
	}
	for (guint i = 0; i < configs->len; i++) {
		XbNode *config = g_ptr_array_index(configs, i);
		XbNode *firmware = NULL;
		const gchar *dev_file_tmp = NULL;

		if (g_strcmp0(xb_node_get_attr(config, "ID"), config_id) != 0)
			continue;
		firmware = xb_node_query_first(config, "Standard/DEVFirmware", NULL);
		if (firmware == NULL)
			continue;
		dev_file_tmp = xb_node_get_attr(firmware, "File");
		if (dev_file_tmp == NULL || dev_file_tmp[0] == '\0') {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "DEV configuration has no image directory");
			return FALSE;
		}
		if (dev_file == NULL) {
			dev_file = dev_file_tmp;
		} else if (g_strcmp0(dev_file, dev_file_tmp) != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "DEV configuration maps to multiple images");
			return FALSE;
		}
	}
	if (dev_file == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "DEV image directory not found");
		return FALSE;
	}

	g_debug("machine %s -> DEV directory %s", matched_product_id, dev_file);
	*dev_file_out = g_strdup(dev_file);
	return TRUE;
}

static GBytes *
fu_mm_mhi_firehose_patch_rawprogram(GBytes *rawprogram_blob,
				    const gchar *filename,
				    GError **error)
{
	gsize rawprogram_sz = 0;
	g_autofree gchar *rawprogram = NULL;
	g_autofree gchar *replacement1 = NULL;
	g_autofree gchar *replacement2 = NULL;
	g_autoptr(GString) xml = NULL;
	guint replaced;

	/* This intentionally handles only the canonical PoC package format. */
	rawprogram =
	    g_strndup(g_bytes_get_data(rawprogram_blob, &rawprogram_sz), rawprogram_sz);
	xml = g_string_new(rawprogram);
	replacement1 =
	    g_strdup_printf("filename=\"%s\" label=\"%s\"", filename,
			    FU_MM_MHI_FIREHOSE_DEV_PARTITION_LABEL);
	replacement2 =
	    g_strdup_printf("label=\"%s\" filename=\"%s\"",
			    FU_MM_MHI_FIREHOSE_DEV_PARTITION_LABEL,
			    filename);
	replaced = g_string_replace(xml,
				    "filename=\"\" label=\"" FU_MM_MHI_FIREHOSE_DEV_PARTITION_LABEL "\"",
				    replacement1,
				    0);
	if (replaced == 0) {
		replaced = g_string_replace(
		    xml,
		    "label=\"" FU_MM_MHI_FIREHOSE_DEV_PARTITION_LABEL "\" filename=\"\"",
		    replacement2,
		    0);
	}
	if (replaced != 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "expected one empty %s filename, found %u",
			    FU_MM_MHI_FIREHOSE_DEV_PARTITION_LABEL,
			    replaced);
		return NULL;
	}
	return g_bytes_new(xml->str, xml->len);
}

gboolean
fu_mm_mhi_firehose_prepare_dev_image(FuFirmware *firmware,
				     const gchar * const *product_ids,
				     GError **error)
{
	g_autofree gchar *dev_file = NULL;
	g_autofree gchar *dev_image_id = NULL;
	g_autofree gchar *dev_image_basename = NULL;
	g_autoptr(GBytes) dev_image_blob = NULL;
	g_autoptr(GBytes) mapping_blob = NULL;
	g_autoptr(GBytes) patched_rawprogram = NULL;
	g_autoptr(FuFirmware) dev_image = NULL;
	g_autoptr(FuFirmware) existing_root_image = NULL;
	g_autoptr(FuFirmware) mapping_image = NULL;
	g_autoptr(FuFirmware) rawprogram = NULL;

	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* Keep using the existing flow for packages without this opt-in table. */
	mapping_image =
	    fu_firmware_get_image_by_id(firmware, FU_MM_MHI_FIREHOSE_DEV_IMAGE_MAPPING_ID, NULL);
	if (mapping_image == NULL)
		return TRUE;
	mapping_blob = fu_firmware_get_bytes(mapping_image, error);
	if (mapping_blob == NULL)
		return FALSE;
	if (product_ids == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no machine product IDs supplied");
		return FALSE;
	}

	if (!fu_mm_mhi_firehose_resolve_dev_file(mapping_blob, product_ids, &dev_file, error))
		return FALSE;
	dev_image_id =
	    g_strdup_printf("DEV_OTA_PACKAGE/%s/devicepack.ubi", dev_file);
	dev_image = fu_firmware_get_image_by_id(firmware, dev_image_id, error);
	if (dev_image == NULL)
		return FALSE;
	dev_image_basename = g_path_get_basename(dev_image_id);
	existing_root_image = fu_firmware_get_image_by_id(firmware, dev_image_basename, NULL);
	if (existing_root_image != NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "DEV image %s already exists",
			    dev_image_basename);
		return FALSE;
	}

	/* qc-firehose resolves images by basename, so expose the selected image at root. */
	dev_image_blob = fu_firmware_get_bytes(dev_image, error);
	if (dev_image_blob == NULL)
		return FALSE;
	{
		g_autoptr(FuFirmware) dev_image_root = fu_zip_file_new();
		fu_firmware_set_id(dev_image_root, dev_image_basename);
		fu_firmware_set_bytes(dev_image_root, dev_image_blob);
		if (!fu_firmware_add_image(firmware, dev_image_root, error))
			return FALSE;
	}

	/* PoC assumption: the package exposes the intended manifest under this glob. */
	rawprogram = fu_firmware_get_image_by_id(firmware, "firehose-rawprogram.xml", NULL);
	if (rawprogram == NULL)
		rawprogram = fu_firmware_get_image_by_id(firmware, "rawprogram_*.xml", error);
	if (rawprogram == NULL)
		return FALSE;
	{
		g_autoptr(GBytes) rawprogram_blob = fu_firmware_get_bytes(rawprogram, error);
		if (rawprogram_blob == NULL)
			return FALSE;
		patched_rawprogram =
		    fu_mm_mhi_firehose_patch_rawprogram(rawprogram_blob,
						       dev_image_basename,
						       error);
		if (patched_rawprogram == NULL)
			return FALSE;
	}
	fu_firmware_set_bytes(rawprogram, patched_rawprogram);
	g_debug("prepared %s for %s", dev_image_basename, dev_image_id);
	return TRUE;
}

static gboolean
fu_mm_mhi_firehose_device_trigger_edl(FuMmMhiFirehoseDevice *self, GError **error)
{
	const gchar *mhi_name = NULL;
	g_autoptr(GPtrArray) attrs = NULL;
	g_autofree gchar *trigger_path = NULL;

	attrs = fu_udev_device_list_sysfs(FU_UDEV_DEVICE(self), error);
	if (attrs == NULL) {
		g_prefix_error_literal(error, "failed to list sysfs entries: ");
		return FALSE;
	}
	for (guint i = 0; i < attrs->len; i++) {
		const gchar *name = g_ptr_array_index(attrs, i);
		if (g_str_has_prefix(name, "mhi")) {
			mhi_name = name;
			break;
		}
	}
	if (mhi_name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no mhi device found");
		return FALSE;
	}

	trigger_path = g_build_filename(mhi_name, "trigger_edl", NULL);
	if (!fu_udev_device_write_sysfs(FU_UDEV_DEVICE(self), trigger_path, "1", 3000, error)) {
		g_prefix_error_literal(error, "write trigger_edl fail: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mhi_firehose_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);
	const gchar *path;
	g_autofree gchar *fn = NULL;

	/* sanity check */
	if (self->search_path_locker == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "search path locker not set");
		return FALSE;
	}
	if (self->firehose_prog_file == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "firehose prog filename is not set for the device");
		return FALSE;
	}

	/* copy the firehose prog into the search path locker */
	path = fu_kernel_search_path_locker_get_path(self->search_path_locker);
	fn = g_build_filename(path, self->firehose_prog_file, NULL);
	if (!fu_bytes_set_contents(fn, self->firehose_prog, error)) {
		g_prefix_error_literal(error, "create firehose prog file fail: ");
		return FALSE;
	}

	/* trigger emergency download mode */
	if (!fu_mm_mhi_firehose_device_trigger_edl(self, error)) {
		g_prefix_error_literal(error, "switch to firehose port fail: ");
		return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static FuKernelSearchPathLocker *
fu_mm_mhi_firehose_device_search_path_locker_new(FuMmMhiFirehoseDevice *self, GError **error)
{
	g_autoptr(FuKernelSearchPathLocker) locker = NULL;

	if (self->lib_firmware_path == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "system firmware path not set");
		return NULL;
	}

	locker = fu_device_kernel_search_path_locker_new(FU_DEVICE(self),
							 self->lib_firmware_path,
							 error);
	if (locker == NULL)
		return NULL;
	return g_steal_pointer(&locker);
}

static gboolean
fu_mm_mhi_firehose_device_prepare(FuDevice *device,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);

	/* in the case of MHI PCI modems, the mhi-pci-generic driver reads the firehose binary
	 * from the firmware-loader and writes it to the modem */
	self->search_path_locker = fu_mm_mhi_firehose_device_search_path_locker_new(self, error);
	if (self->search_path_locker == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mhi_firehose_device_cleanup(FuDevice *device,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);

	/* restore the firmware search path */
	g_clear_object(&self->search_path_locker);

	/* no longer required */
	g_clear_pointer(&self->firehose_prog, g_bytes_unref);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_mm_mhi_firehose_device_prepare_firmware(FuDevice *device,
					   FuInputStream *stream,
					   FuProgress *progress,
					   FuFirmwareParseFlags flags,
					   GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);
	FuContext *ctx = fu_device_get_context(device);
	const gchar *product_ids[5] = {NULL};
	guint product_ids_idx = 0;
	const gchar *product_id = NULL;
	g_autoptr(FuFirmware) firmware = fu_zip_firmware_new();

	/* parse as archive */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* firehose modems that use mhi_pci drivers require firehose binary
	 * to be present in the firmware-loader search path. */
	self->firehose_prog =
	    fu_firmware_get_image_by_id_bytes(firmware,
					      "firehose-prog.mbn|prog_nand*.mbn|prog_firehose*",
					      error);
	if (self->firehose_prog == NULL)
		return NULL;

	if (self->wwan_device_id != NULL && self->wwan_device_id[0] != '\0') {
		product_ids[product_ids_idx++] = self->wwan_device_id;
	} else {
		product_id = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_SKU);
		if (product_id != NULL)
			product_ids[product_ids_idx++] = product_id;
		product_id = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_NAME);
		if (product_id != NULL)
			product_ids[product_ids_idx++] = product_id;
		product_id = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BASEBOARD_PRODUCT);
		if (product_id != NULL)
			product_ids[product_ids_idx++] = product_id;
	}
	if (!fu_mm_mhi_firehose_prepare_dev_image(firmware, product_ids, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_mm_mhi_firehose_device_set_quirk_kv(FuDevice *device,
				       const gchar *key,
				       const gchar *value,
				       GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);

	if (g_strcmp0(key, "ModemManagerFirehoseProgFile") == 0) {
		g_set_str(&self->firehose_prog_file, value);
		return TRUE;
	}

	if (g_strcmp0(key, "ModemManagerLibFirmwarePath") == 0) {
		g_set_str(&self->lib_firmware_path, value);
		return TRUE;
	}

	if (g_strcmp0(key, "ModemManagerWwanDeviceId") == 0) {
		g_set_str(&self->wwan_device_id, value);
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static gboolean
fu_mm_mhi_firehose_device_probe(FuDevice *device, GError **error)
{
	gboolean devnode_exists = FALSE;
	const gchar *sysfs_path;
	const gchar *mhi_name = NULL;
	g_autoptr(GPtrArray) attrs = NULL;
	g_autofree gchar *devnode = NULL;

	sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	if (sysfs_path == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "failed to get sysfs path");
		return FALSE;
	}

	attrs = fu_udev_device_list_sysfs(FU_UDEV_DEVICE(device), error);
	if (attrs == NULL) {
		g_prefix_error_literal(error, "failed to list sysfs entries: ");
		return FALSE;
	}
	for (guint i = 0; i < attrs->len; i++) {
		const gchar *name = g_ptr_array_index(attrs, i);
		if (g_str_has_prefix(name, "mhi")) {
			mhi_name = name;
			break;
		}
	}
	if (mhi_name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no mhi device found");
		return FALSE;
	}

	/* in the mhi_pci_generic driver, mbim channel name definitely be configured as MBIM
	 * skip until emulation is re-recorded */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
		devnode = g_strdup_printf("%s/%s/%s_MBIM", sysfs_path, mhi_name, mhi_name);
		if (!fu_device_query_file_exists(device, devnode, &devnode_exists, error))
			return FALSE;
		if (!devnode_exists) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no mbim port found at %s",
				    devnode);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_mm_mhi_firehose_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_mm_mhi_firehose_device_init(FuMmMhiFirehoseDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), 30000);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_protocol(FU_UDEV_DEVICE(self), "com.qualcomm.firehose");
}

static void
fu_mm_mhi_firehose_device_finalize(GObject *object)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(object);

	g_clear_object(&self->search_path_locker);
	g_free(self->firehose_prog_file);
	g_free(self->lib_firmware_path);
	g_free(self->wwan_device_id);
	if (self->firehose_prog != NULL)
		g_bytes_unref(self->firehose_prog);
	G_OBJECT_CLASS(fu_mm_mhi_firehose_device_parent_class)->finalize(object);
}

static void
fu_mm_mhi_firehose_device_class_init(FuMmMhiFirehoseDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_mm_mhi_firehose_device_finalize;
	device_class->probe = fu_mm_mhi_firehose_device_probe;
	device_class->detach = fu_mm_mhi_firehose_device_detach;
	device_class->prepare = fu_mm_mhi_firehose_device_prepare;
	device_class->cleanup = fu_mm_mhi_firehose_device_cleanup;
	device_class->prepare_firmware = fu_mm_mhi_firehose_device_prepare_firmware;
	device_class->set_quirk_kv = fu_mm_mhi_firehose_device_set_quirk_kv;
	device_class->set_progress = fu_mm_mhi_firehose_device_set_progress;
}
