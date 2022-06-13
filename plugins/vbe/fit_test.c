/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Tests for libfit
 *
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib-2.0/glib.h>
#include <libfdt.h>
#include <stdio.h>

#include "fit.h"

/**
 * enum gen_t: Options to control the output of the test FIT
 *
 * @GEN_CFGS: Generate the /configurations node
 * @GEN_CFG: Generate a configuration inside /configurations
 * @GEN_COMPAT: Generate a compatible string in the configuration
 * @GEN_IMGS: Generate the /images node
 * @GEN_IMG: Generate an image inside /images
 * @GEN_DATA: Generate some (internal) data for the image
 * @GEN_EXT_DATA: Generate external data in the FIT
 * @GEN_DATA_SIZE: Generate the correct data-size property
 * @GEN_CRC32_ALGO: Generate an 'algo' property for CRC32
 * @GEN_CRC32_VAL: Generate a crc32 value for the data
 * @GEN_CRC32_BAD_SIZE: Generate a crc32 value with a bad size
 * @GEN_CRC32_BAD_VAL: Generate a bad crc32 value for the data
 * @GEN_BAD_ALGO: Generate an unknown algo property
 * @GEN_STORE_OFFSET: Generate a store-offset for the image
 * @GEN_BAD_DATA_SIZE: Generate a negative data-size property
 * @GEN_BAD_DATA_OFFSET: Generate a negative data-offset property
 * @GEN_BAD_STORE_OFFSET: Generate a negative store-offset property
 * @GEN_SKIP_OFFSET: Generate a skip-offset property
 * @GEN_BAD_SKIP_OFFSET: Generate a bad negative skip-offset property
 * @GEN_VERSION: Generate a version for the config
 */
enum gen_t {
	GEN_CFGS = 1 << 0,
	GEN_CFG = 1 << 1,
	GEN_COMPAT = 1 << 2,
	GEN_IMGS = 1 << 3,
	GEN_IMG = 1 << 4,
	GEN_DATA = 1 << 5,
	GEN_EXT_DATA = 1 << 6,
	GEN_DATA_SIZE = 1 << 7,
	GEN_CRC32_ALGO = 1 << 8,
	GEN_CRC32_VAL = 1 << 9,
	GEN_CRC32_BAD_SIZE = 1 << 10,
	GEN_CRC32_BAD_VAL = 1 << 11,
	GEN_BAD_ALGO = 1 << 12,
	GEN_STORE_OFFSET = 1 << 13,
	GEN_BAD_STORE_OFFSET = 1 << 14,
	GEN_BAD_DATA_SIZE = 1 << 15,
	GEN_BAD_DATA_OFFSET = 1 << 16,
	GEN_SKIP_OFFSET = 1 << 17,
	GEN_BAD_SKIP_OFFSET = 1 << 18,
	GEN_VERSION = 1 << 19,
};

/* Size of the test FIT we use */
#define FIT_SIZE 1024

/**
 * struct FitTest - Working data used by FIT tests
 *
 * @fit: fit_info structure to use
 * @buf: buffer containing the test FIT
 */
typedef struct FitTest {
	struct fit_info fit;
	char buf[FIT_SIZE];
} FitTest;

/**
 * build_fit() - Build a Flat Image Tree with various options
 *
 * @buf: Place to put the FIT
 * @size: Size of the FIT in bytes
 * @flags: Mask of 'enum gen_t' controlling what is generated
 *
 */
static int
build_fit(char *buf, int size, int flags)
{
	const int data_offset = 4;

	fdt_create(buf, size);
	fdt_finish_reservemap(buf);

	fdt_begin_node(buf, "");

	/* / */
	fdt_property_u32(buf, "timestamp", 0x629d4abd);
	fdt_property_string(buf, "description", "FIT description");
	fdt_property_string(buf, "creator", "FIT test");

	if (flags & GEN_IMGS) {
		/* /images */
		fdt_begin_node(buf, "images");

		if (flags & GEN_IMG) {
			/* /images/firmware-1 */
			fdt_begin_node(buf, "firmware-1");
			fdt_property_string(buf, "description", "v1.2.4");
			fdt_property_string(buf, "type", "firmware");
			fdt_property_string(buf, "arch", "arm64");
			fdt_property_string(buf, "os", "u-boot");
			fdt_property_string(buf, "compression", "none");
			fdt_property_u32(buf, "load", 0x100);
			fdt_property_u32(buf, "entry", 0x100);

			if (flags & GEN_STORE_OFFSET)
				fdt_property_u32(buf, "store-offset", 0x1000);
			if (flags & GEN_BAD_STORE_OFFSET)
				fdt_property_u32(buf, "store-offset", -4);
			if (flags & GEN_SKIP_OFFSET)
				fdt_property_u32(buf, "skip-offset", 4);
			if (flags & GEN_BAD_SKIP_OFFSET)
				fdt_property_u32(buf, "skip-offset", -4);

			if (flags & GEN_DATA)
				fdt_property(buf, "data", "abc", 3);

			if (flags & GEN_EXT_DATA) {
				if (flags & GEN_BAD_DATA_OFFSET) {
					fdt_property_u32(buf, "data-offset", -3);
				} else {
					fdt_property_u32(buf, "data-offset", data_offset);
				}
				if (flags & GEN_DATA_SIZE)
					fdt_property_u32(buf, "data-size", 3);
				if (flags & GEN_BAD_DATA_SIZE)
					fdt_property_u32(buf, "data-size", -3);
			}

			/* /images/firmware-1/hash-1 */
			fdt_begin_node(buf, "hash-1");
			if (flags & GEN_CRC32_ALGO)
				fdt_property_string(buf, "algo", "crc32");
			if (flags & GEN_BAD_ALGO)
				fdt_property_string(buf, "algo", "wibble");

			/* This is the correct crc32 for "abc" */
			if (flags & GEN_CRC32_VAL)
				fdt_property_u32(buf, "value", 0x4788814e);
			if (flags & GEN_CRC32_BAD_VAL)
				fdt_property_u32(buf, "value", 0xa738ea1c);
			if (flags & GEN_CRC32_BAD_SIZE)
				fdt_property(buf, "value", "hi", 2);
			fdt_end_node(buf);

			/* /images/firmware-1 */
			fdt_end_node(buf);
		}

		/* /images */
		fdt_end_node(buf);
	}

	if (flags & GEN_CFGS) {
		/* /configurations */
		fdt_begin_node(buf, "configurations");
		fdt_property_string(buf, "default", "conf-1");

		if (flags & GEN_CFG) {
			/* /configurations/conf-1 */
			fdt_begin_node(buf, "conf-1");
			fdt_property_string(buf, "firmware", "firmware-1");
			if (flags & GEN_COMPAT)
				fdt_property_string(buf, "compatible", "mary");
			if (flags & GEN_VERSION)
				fdt_property_string(buf, "version", "1.2.3");
			fdt_end_node(buf);
		}

		/* /configurations */
		fdt_end_node(buf);
	}

	/* / */
	fdt_end_node(buf);
	fdt_finish(buf);

	if (flags & GEN_EXT_DATA) {
		char *data;

		data = buf + ((fdt_totalsize(buf) + 3) & ~3) + data_offset;
		strcpy(data, "abc");
	}

	g_assert_cmpint(fdt_totalsize(buf), <=, FIT_SIZE);

	return 0;
};

/* Test an invalid FIT */
static void
test_base(FitTest *ftest, gconstpointer user_data)
{
	struct fit_info *fit = &ftest->fit;
	char *buf = ftest->buf;

	/* Bad FIT */
	strcpy(buf, "junk");
	g_assert_cmpint(-FITE_BAD_HEADER, ==, fit_open(fit, buf, FIT_SIZE));

	/* FIT with missing /configurations */
	g_assert_cmpint(0, ==, build_fit(buf, FIT_SIZE, 0));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));
	fit_close(fit);
}

/* Test a FIT with configuration but not images */
static void
test_cfg(FitTest *ftest, gconstpointer user_data)
{
	struct fit_info *fit = &ftest->fit;
	char *buf = ftest->buf;
	int cfg;

	g_assert_cmpint(0, ==, build_fit(buf, FIT_SIZE, 0));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));
	cfg = fit_first_cfg(fit);
	g_assert_cmpint(-FITE_NO_CONFIG_NODE, ==, cfg);

	/* FIT with missing configuration */
	g_assert_cmpint(0, ==, build_fit(buf, FIT_SIZE, GEN_CFGS));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	g_assert_cmpint(-FITE_NOT_FOUND, ==, cfg);
	fit_close(fit);

	/* Normal FIT without compatible string */
	g_assert_cmpint(0, ==, build_fit(buf, FIT_SIZE, GEN_CFGS | GEN_CFG));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	g_assert_cmpint(cfg, >, 0);
	g_assert_cmpstr("conf-1", ==, fit_cfg_name(fit, cfg));

	g_assert_null(fit_cfg_compat_item(fit, cfg, 0));
	fit_close(fit);

	/* Normal FIT with compatible string but no /images node */
	g_assert_cmpint(0, ==, build_fit(buf, FIT_SIZE, GEN_CFGS | GEN_CFG | GEN_COMPAT));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	g_assert_cmpint(cfg, >, 0);
	g_assert_cmpstr("conf-1", ==, fit_cfg_name(fit, cfg));

	g_assert_cmpstr("mary", ==, fit_cfg_compat_item(fit, cfg, 0));

	g_assert_cmpint(-FITE_NOT_FOUND, ==, fit_cfg_img_count(fit, cfg, "fred"));
	g_assert_cmpint(-FITE_NOT_FOUND, ==, fit_cfg_img(fit, cfg, "fred", 0));
	g_assert_cmpint(1, ==, fit_cfg_img_count(fit, cfg, "firmware"));

	g_assert_cmpint(-FITE_NO_IMAGES_NODE, ==, fit_cfg_img(fit, cfg, "firmware", 0));
	fit_close(fit);
}
/* Normal FIT with compatible string and image but no data */
static void
test_img(FitTest *ftest, gconstpointer user_data)
{
	struct fit_info *fit = &ftest->fit;
	char *buf = ftest->buf;
	int size, cfg, img;
	const char *data;

	g_assert_cmpint(0,
			==,
			build_fit(buf, FIT_SIZE, GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	g_assert_cmpint(-FITE_MISSING_IMAGE, ==, fit_cfg_img(fit, cfg, "firmware", 0));
	g_assert_cmpint(-FITE_NOT_FOUND, ==, fit_cfg_img(fit, cfg, "firmware", 1));
	fit_close(fit);

	/* With an image as well */
	g_assert_cmpint(
	    0,
	    ==,
	    build_fit(buf, FIT_SIZE, GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_NOT_FOUND, ==, size);
	fit_close(fit);
}

/* Normal FIT with data as well */
static void
test_data(FitTest *ftest, gconstpointer user_data)
{
	struct fit_info *fit = &ftest->fit;
	char *buf = ftest->buf;
	int size, cfg, img;
	const char *data;

	/* With data as well */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_DATA));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	data = fit_img_data(fit, img, &size);
	g_assert_cmpint(3, ==, size);
	g_assert_cmpint(0, ==, strncmp(data, "abc", 3));

	cfg = fit_next_cfg(fit, cfg);
	g_assert_cmpint(-FITE_NOT_FOUND, ==, cfg);
}

/* Normal FIT with external data */
static void
test_ext_data(FitTest *ftest, gconstpointer user_data)
{
	struct fit_info *fit = &ftest->fit;
	char *buf = ftest->buf;
	int size, cfg, img;
	const char *data;

	/* Test with missing data-size property */
	g_assert_cmpint(
	    0,
	    ==,
	    build_fit(buf,
		      FIT_SIZE,
		      GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_EXT_DATA));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);

	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_MISSING_SIZE, ==, size);
	fit_close(fit);

	/* Test with bad data-size property */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG |
				      GEN_EXT_DATA | GEN_BAD_DATA_SIZE));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);

	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_NEGATIVE_SIZE, ==, size);
	fit_close(fit);

	/* Test with bad data-offset property */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG |
				      GEN_EXT_DATA | GEN_DATA_SIZE | GEN_BAD_DATA_OFFSET));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);

	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_NEGATIVE_OFFSET, ==, size);
	fit_close(fit);

	/* Test with valid data-size property */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG |
				      GEN_EXT_DATA | GEN_DATA_SIZE));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);

	data = fit_img_data(fit, img, &size);
	g_assert_cmpint(3, ==, size);
	g_assert_nonnull(data);
	g_assert_cmpint(0, ==, strncmp(data, "abc", 3));
	fit_close(fit);
}

/* Check data with CRC32 */
static void
test_crc32(FitTest *ftest, gconstpointer user_data)
{
	struct fit_info *fit = &ftest->fit;
	char *buf = ftest->buf;
	int size, cfg, img;
	const char *data;
	int node;

	/* Missing 'algo' property gives an error when 'value' is provided */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_DATA |
				      GEN_CRC32_VAL));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_MISSING_ALGO, ==, size);
	fit_close(fit);

	/* Unknown 'algo' property gives an error when 'value' is provided */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_DATA |
				      GEN_BAD_ALGO | GEN_CRC32_VAL));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_UNKNOWN_ALGO, ==, size);
	fit_close(fit);

	/* Missing 'value' property means the hash is ignored */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_DATA |
				      GEN_CRC32_ALGO));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	data = fit_img_data(fit, img, &size);
	g_assert_nonnull(data);
	g_assert_cmpint(0, ==, strncmp(data, "abc", 3));

	/* ...but we can see that the hash value as missing */
	node = fdt_first_subnode(fit->blob, img);
	g_assert_cmpint(node, >, 0);
	g_assert_cmpint(-FITE_MISSING_VALUE, ==, fit_check_hash(fit, node, "abc", 3));
	fit_close(fit);

	/* 'value' and 'algo' present but the value size is wrong */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_DATA |
				      GEN_CRC32_ALGO | GEN_CRC32_BAD_SIZE));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_INVALID_HASH_SIZE, ==, size);
	fit_close(fit);

	/* 'value' and 'algo' present but the value is wrong */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_DATA |
				      GEN_CRC32_ALGO | GEN_CRC32_BAD_VAL));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_HASH_MISMATCH, ==, size);
	fit_close(fit);

	/* 'value' and 'algo' present with correct value */
	g_assert_cmpint(0,
			==,
			build_fit(buf,
				  FIT_SIZE,
				  GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_DATA |
				      GEN_CRC32_ALGO | GEN_CRC32_VAL));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	data = fit_img_data(fit, img, &size);
	g_assert_null(data);
	g_assert_cmpint(-FITE_HASH_MISMATCH, ==, size);
	fit_close(fit);
}

/* Check data with store-offset */
static void
test_store_offset(FitTest *ftest, gconstpointer user_data)
{
	struct fit_info *fit = &ftest->fit;
	char *buf = ftest->buf;
	int cfg, img;

	/* Missing 'store-offset' property */
	g_assert_cmpint(
	    0,
	    ==,
	    build_fit(buf, FIT_SIZE, GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	g_assert_cmpint(-FITE_NOT_FOUND, ==, fit_img_store_offset(fit, img));
	fit_close(fit);

	/* Negative 'store-offset' property */
	g_assert_cmpint(
	    0,
	    ==,
	    build_fit(buf,
		      FIT_SIZE,
		      GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_BAD_STORE_OFFSET));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	g_assert_cmpint(-FITE_NEGATIVE_OFFSET, ==, fit_img_store_offset(fit, img));
	fit_close(fit);

	/* Valid 'store-offset' property */
	g_assert_cmpint(
	    0,
	    ==,
	    build_fit(buf,
		      FIT_SIZE,
		      GEN_CFGS | GEN_CFG | GEN_COMPAT | GEN_IMGS | GEN_IMG | GEN_STORE_OFFSET));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	img = fit_cfg_img(fit, cfg, "firmware", 0);
	g_assert_cmpint(img, >, 0);

	g_assert_cmpstr("firmware-1", ==, fit_img_name(fit, img));
	g_assert_cmpint(0x1000, ==, fit_img_store_offset(fit, img));
	fit_close(fit);
}

/* Check getting config version */
static void
test_version(FitTest *ftest, gconstpointer user_data)
{
	struct fit_info *fit = &ftest->fit;
	char *buf = ftest->buf;
	const char *version;
	int cfg;

	/* Missing 'version' property */
	g_assert_cmpint(0, ==, build_fit(buf, FIT_SIZE, GEN_CFGS | GEN_CFG));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	version = fit_cfg_version(fit, cfg);
	g_assert_null(version);

	/* With version */
	g_assert_cmpint(0, ==, build_fit(buf, FIT_SIZE, GEN_CFGS | GEN_CFG | GEN_VERSION));
	g_assert_cmpint(0, ==, fit_open(fit, buf, FIT_SIZE));

	cfg = fit_first_cfg(fit);
	version = fit_cfg_version(fit, cfg);
	g_assert_cmpstr("1.2.3", ==, version);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_test_add("/fit/base", FitTest, NULL, NULL, test_base, NULL);
	g_test_add("/fit/cfg", FitTest, NULL, NULL, test_cfg, NULL);
	g_test_add("/fit/img", FitTest, NULL, NULL, test_img, NULL);
	g_test_add("/fit/data", FitTest, NULL, NULL, test_data, NULL);
	g_test_add("/fit/ext_data", FitTest, NULL, NULL, test_ext_data, NULL);
	g_test_add("/fit/crc32", FitTest, NULL, NULL, test_crc32, NULL);
	g_test_add("/fit/store_offset", FitTest, NULL, NULL, test_store_offset, NULL);
	g_test_add("/fit/version", FitTest, NULL, NULL, test_version, NULL);

	return g_test_run();
}
