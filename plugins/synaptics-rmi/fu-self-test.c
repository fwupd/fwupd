/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-synaptics-rmi-device.h"
#include "fu-synaptics-rmi-firmware.h"
#include "fu-synaptics-rmi-v5-device.h"
#include "fu-synaptics-rmi-v6-device.h"
#include "fu-synaptics-rmi-v7-device.h"

/* a device that answers different PDT scans, dropping F01 or F34 in subsequent rounds */
#define FU_TYPE_SYNAPTICS_RMI_MOCK_DEVICE (fu_synaptics_rmi_mock_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsRmiMockDevice,
		     fu_synaptics_rmi_mock_device,
		     FU,
		     SYNAPTICS_RMI_MOCK_DEVICE,
		     FuSynapticsRmiDevice)

struct _FuSynapticsRmiMockDevice {
	FuSynapticsRmiDevice parent_instance;
	guint scan_round;
};

G_DEFINE_TYPE(FuSynapticsRmiMockDevice, fu_synaptics_rmi_mock_device, FU_TYPE_SYNAPTICS_RMI_DEVICE)

/* PDT entry: query, command, control, data, (version << 5) | irq-sources, number */
static const guint8 pdt_f34[] = {0x20, 0x21, 0x22, 0x23, 0x41, 0x34};
static const guint8 pdt_f01[] = {0x10, 0x11, 0x12, 0x13, 0x01, 0x01};
static const guint8 pdt_end[RMI_DEVICE_PDT_ENTRY_SIZE] = {0};

static GByteArray *
fu_synaptics_rmi_mock_device_read(FuSynapticsRmiDevice *device,
				  guint16 addr,
				  gsize req_sz,
				  GError **error)
{
	FuSynapticsRmiMockDevice *self = FU_SYNAPTICS_RMI_MOCK_DEVICE(device);
	const guint8 *src = pdt_end;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (self->scan_round == 0) {
		if (addr == 0x00e9)
			src = pdt_f34;
		else if (addr == 0x00e3)
			src = pdt_f01;
	} else if (self->scan_round == 1) {
		if (addr == 0x00e9)
			src = pdt_f34;
	} else if (self->scan_round == 2) {
		if (addr == 0x00e9)
			src = pdt_f01;
	}

	/* return the requested size, zero-padded if larger than the source entry */
	fu_byte_array_set_size(buf, req_sz, 0x0);
	if (!fu_memcpy_safe(buf->data,
			    buf->len,
			    0x0,
			    src,
			    RMI_DEVICE_PDT_ENTRY_SIZE,
			    0x0,
			    MIN(req_sz, RMI_DEVICE_PDT_ENTRY_SIZE),
			    error))
		return NULL;
	return g_steal_pointer(&buf);
}

static gboolean
fu_synaptics_rmi_mock_device_write(FuSynapticsRmiDevice *device,
				   guint16 addr,
				   GByteArray *req,
				   FuSynapticsRmiDeviceFlags flags,
				   GError **error)
{
	return TRUE;
}

static gboolean
fu_synaptics_rmi_mock_device_set_page(FuSynapticsRmiDevice *device, guint8 page, GError **error)
{
	return TRUE;
}

static gboolean
fu_synaptics_rmi_mock_device_wait_for_attr(FuSynapticsRmiDevice *device,
					   guint8 source_mask,
					   guint timeout_ms,
					   GError **error)
{
	return TRUE;
}

static void
fu_synaptics_rmi_mock_device_init(FuSynapticsRmiMockDevice *self)
{
}

static void
fu_synaptics_rmi_mock_device_class_init(FuSynapticsRmiMockDeviceClass *klass)
{
	FuSynapticsRmiDeviceClass *rmi_class = FU_SYNAPTICS_RMI_DEVICE_CLASS(klass);
	rmi_class->read = fu_synaptics_rmi_mock_device_read;
	rmi_class->write = fu_synaptics_rmi_mock_device_write;
	rmi_class->set_page = fu_synaptics_rmi_mock_device_set_page;
	rmi_class->wait_for_attr = fu_synaptics_rmi_mock_device_wait_for_attr;
}

static void
fu_synaptics_rmi_device_pdt_rescan_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSynapticsRmiMockDevice) device =
	    g_object_new(FU_TYPE_SYNAPTICS_RMI_MOCK_DEVICE, "context", ctx, NULL);
	g_autoptr(GError) error = NULL;

	fu_synaptics_rmi_device_set_max_page(FU_SYNAPTICS_RMI_DEVICE(device), 1);

	/* initial PDT has both F34 and F01 */
	ret = fu_synaptics_rmi_device_scan_pdt(FU_SYNAPTICS_RMI_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* caches F01 in round 0 */
	ret = fu_synaptics_rmi_device_reset(FU_SYNAPTICS_RMI_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* caches F34 in round 0 */
	ret = fu_synaptics_rmi_device_wait_for_idle(FU_SYNAPTICS_RMI_DEVICE(device),
						    0,
						    FU_SYNAPTICS_RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
						    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the device drops F01 and we rescan: cached functions are freed & refreshed */
	device->scan_round = 1;
	ret = fu_synaptics_rmi_device_scan_pdt(FU_SYNAPTICS_RMI_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* must fail cleanly with NOT_SUPPORTED rather than dereferencing freed/NULL F01 */
	ret = fu_synaptics_rmi_device_disable_irqs(FU_SYNAPTICS_RMI_DEVICE(device), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);

	/* the device drops F34 and keeps F01 on next rescan */
	g_clear_error(&error);
	device->scan_round = 2;
	ret = fu_synaptics_rmi_device_scan_pdt(FU_SYNAPTICS_RMI_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* must fail cleanly with NOT_SUPPORTED rather than dereferencing freed/NULL F34 */
	ret = fu_synaptics_rmi_device_disable_irqs(FU_SYNAPTICS_RMI_DEVICE(device), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_synaptics_rmi_firmware_0x_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "synaptics-rmi-0x.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "8b097c034028a69e6416bcc39f312e2fa9247381",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_synaptics_rmi_firmware_10_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "synaptics-rmi-10.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "bd85539bb100e5bd6debb00b06b5a7e7fa9bd030",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static FuSynapticsRmiDevice *
fu_synaptics_rmi_test_new_scanned_device(FuContext *ctx)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	FuSynapticsRmiDevice *device =
	    g_object_new(FU_TYPE_SYNAPTICS_RMI_MOCK_DEVICE, "context", ctx, NULL);

	fu_synaptics_rmi_device_set_max_page(device, 1);
	ret = fu_synaptics_rmi_device_scan_pdt(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	return device;
}

static void
fu_synaptics_rmi_v5_setup_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSynapticsRmiDevice) device = fu_synaptics_rmi_test_new_scanned_device(ctx);
	g_autoptr(GError) error = NULL;

	ret = fu_synaptics_rmi_v5_device_setup(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_synaptics_rmi_v6_setup_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSynapticsRmiDevice) device = fu_synaptics_rmi_test_new_scanned_device(ctx);
	g_autoptr(GError) error = NULL;

	ret = fu_synaptics_rmi_v6_device_setup(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_synaptics_rmi_v7_setup_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuSynapticsRmiDevice) device = fu_synaptics_rmi_test_new_scanned_device(ctx);
	g_autoptr(GError) error = NULL;

	/* the mock returns a zeroed F34 query, so setup fails the block-size check */
	ret = fu_synaptics_rmi_v7_device_setup(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_SYNAPTICS_RMI_FIRMWARE);
	g_test_add_func("/synaptics-rmi/firmware-0x", fu_synaptics_rmi_firmware_0x_func);
	g_test_add_func("/synaptics-rmi/firmware-10", fu_synaptics_rmi_firmware_10_func);
	g_test_add_func("/synaptics-rmi/pdt-rescan", fu_synaptics_rmi_device_pdt_rescan_func);
	g_test_add_func("/synaptics-rmi/v5-setup", fu_synaptics_rmi_v5_setup_func);
	g_test_add_func("/synaptics-rmi/v6-setup", fu_synaptics_rmi_v6_setup_func);
	g_test_add_func("/synaptics-rmi/v7-setup", fu_synaptics_rmi_v7_setup_func);
	return g_test_run();
}
