/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-nitrokey-common.h"

static void
fu_nitrokey_version_test (void)
{
	/* use the Nitrokey Storage v0.53 status response for test, CRC 0xa2762d14 */
	NitrokeyGetDeviceStatusPayload payload;
	NitrokeyHidResponse res;
	guint32 crc_tmp;
	/* 65 bytes of response from HIDAPI; first byte is always 0 */
	const guint8 buf[] = {
		/*0x00,*/
		0x00, 0x2e, 0xef, 0xc4, 0x9b, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x2e, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x1c, 0x18, 0x33,
		0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x45, 0x24, 0xf1, 0x4c, 0x01, 0x00,
		0x03, 0x03, 0xc7, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x2d, 0x76,
		0xa2 };

	/* testing the whole path, as in fu_nitrokey_device_setup()*/
	memcpy (&res, buf, sizeof (buf));
	memcpy (&payload, &res.payload, sizeof (payload));

	/* verify the version number */
	g_assert_cmpint (payload.VersionMajor, == , 0);
	g_assert_cmpint (payload.VersionMinor, == , 53);
	g_assert_cmpint (buf[34], == , payload.VersionMinor);
	g_assert_cmpint (payload.VersionBuildIteration, == , 0);

	/* verify the response checksum */
	crc_tmp = fu_nitrokey_perform_crc32 (buf, sizeof (res) - 4);
	g_assert_cmpint (GUINT32_FROM_LE (res.crc), == , crc_tmp);

}

static void
fu_nitrokey_version_test_static (void)
{
	/* use static response from numbered bytes, to make sure fields occupy
	 * expected bytes */
	NitrokeyGetDeviceStatusPayload payload;
	NitrokeyHidResponse res;

	const guint8 buf[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
		0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
		0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
	};
	memcpy (&res, buf, sizeof (buf));
	memcpy (&payload, &res.payload, sizeof (payload));
	g_assert_cmpint (payload.VersionMajor, == , 33); /* 0x1a */
	g_assert_cmpint (payload.VersionMinor, == , 34); /* 0x1b */
	g_assert_cmpint (buf[34], == , 34);
}

static void
fu_nitrokey_func (void)
{
	const guint8 buf[] =  { 0x00, 0x01, 0x02, 0x03,
				0x04, 0x05, 0x06, 0x07,
				0x08, 0x09, 0x0a, 0x0b,
				0x0c, 0x0d, 0x0e, 0x0f };
	g_assert_cmpint (fu_nitrokey_perform_crc32 (buf, 16), ==, 0x081B46CA);
	g_assert_cmpint (fu_nitrokey_perform_crc32 (buf, 15), ==, 0xED7320AB);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/fwupd/nitrokey", fu_nitrokey_func);
	g_test_add_func ("/fwupd/nitrokey-version-static", fu_nitrokey_version_test_static);
	g_test_add_func ("/fwupd/nitrokey-version", fu_nitrokey_version_test);
	return g_test_run ();
}
