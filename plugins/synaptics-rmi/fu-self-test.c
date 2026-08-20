/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-synaptics-rmi-device.h"
#include "fu-synaptics-rmi-firmware.h"

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

/**
 * fu_synaptics_rmi_mock_device_read:
 * @device: a #FuSynapticsRmiDevice
 * @addr: register address to read
 * @req_sz: size of buffer requested
 * @error: (nullable): #GError
 *
 * Mocks reading PDT entries across scan rounds.
 *
 * Returns: (transfer full): byte array of register data
 **/
static GByteArray *
fu_synaptics_rmi_mock_device_read(FuSynapticsRmiDevice *device,
				  guint16 addr,
				  gsize req_sz,
				  GError **error)
{
	FuSynapticsRmiMockDevice *self = FU_SYNAPTICS_RMI_MOCK_DEVICE(device);
	const guint8 *src = pdt_end;

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
	return g_byte_array_new_take(g_memdup2(src, req_sz), req_sz);
}

/**
 * fu_synaptics_rmi_mock_device_write:
 * @device: a #FuSynapticsRmiDevice
 * @addr: register address to write
 * @req: data buffer to write
 * @flags: write flags
 * @error: (nullable): #GError
 *
 * Mocks a successful device write operation.
 *
 * Returns: %TRUE on success
 **/
static gboolean
fu_synaptics_rmi_mock_device_write(FuSynapticsRmiDevice *device,
				   guint16 addr,
				   GByteArray *req,
				   FuSynapticsRmiDeviceFlags flags,
				   GError **error)
{
	return TRUE;
}

/**
 * fu_synaptics_rmi_mock_device_set_page:
 * @device: a #FuSynapticsRmiDevice
 * @page: register page number
 * @error: (nullable): #GError
 *
 * Mocks switching the current register page.
 *
 * Returns: %TRUE on success
 **/
static gboolean
fu_synaptics_rmi_mock_device_set_page(FuSynapticsRmiDevice *device, guint8 page, GError **error)
{
	return TRUE;
}

/**
 * fu_synaptics_rmi_mock_device_wait_for_attr:
 * @device: a #FuSynapticsRmiDevice
 * @source_mask: interrupt source mask
 * @timeout_ms: timeout in milliseconds
 * @error: (nullable): #GError
 *
 * Mocks waiting for device interrupts.
 *
 * Returns: %TRUE on success
 **/
static gboolean
fu_synaptics_rmi_mock_device_wait_for_attr(FuSynapticsRmiDevice *device,
					   guint8 source_mask,
					   guint timeout_ms,
					   GError **error)
{
	return TRUE;
}

/**
 * fu_synaptics_rmi_mock_device_init:
 * @self: a #FuSynapticsRmiMockDevice
 *
 * Initializes the mock device instance.
 **/
static void
fu_synaptics_rmi_mock_device_init(FuSynapticsRmiMockDevice *self)
{
}

/**
 * fu_synaptics_rmi_mock_device_class_init:
 * @klass: a #FuSynapticsRmiMockDeviceClass
 *
 * Initializes the mock device class and overrides vmethods.
 **/
static void
fu_synaptics_rmi_mock_device_class_init(FuSynapticsRmiMockDeviceClass *klass)
{
	FuSynapticsRmiDeviceClass *rmi_class = FU_SYNAPTICS_RMI_DEVICE_CLASS(klass);
	rmi_class->read = fu_synaptics_rmi_mock_device_read;
	rmi_class->write = fu_synaptics_rmi_mock_device_write;
	rmi_class->set_page = fu_synaptics_rmi_mock_device_set_page;
	rmi_class->wait_for_attr = fu_synaptics_rmi_mock_device_wait_for_attr;
}

/**
 * fu_synaptics_rmi_device_pdt_rescan_func:
 *
 * Tests that PDT rescan properly invalidates cached function pointers
 * when F01 or F34 is no longer advertised by the device.
 **/
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

/**
 * main:
 * @argc: argument count
 * @argv: argument vector
 *
 * Runs synaptics-rmi unit tests.
 *
 * Returns: 0 on success, non-zero on failure
 **/
int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_SYNAPTICS_RMI_FIRMWARE);
	g_test_add_func("/synaptics-rmi/firmware-0x", fu_synaptics_rmi_firmware_0x_func);
	g_test_add_func("/synaptics-rmi/firmware-10", fu_synaptics_rmi_firmware_10_func);
	g_test_add_func("/synaptics-rmi/pdt-rescan", fu_synaptics_rmi_device_pdt_rescan_func);
	return g_test_run();
}
