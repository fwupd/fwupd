/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Library for U-Boot Flat Image Tree (FIT)
 *
 * This tries to avoid using glib so that it can be used in other projects more
 * easily
 *
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <libfdt.h>
#include <zlib.h>

#include "fit.h"

/* Node and property names used by FIT */
#define FIT_CONFIG_PATH "/configurations"
#define FIT_IMAGE_PATH	"/images"

#define FIT_PROP_COMPATIBLE   "compatible"
#define FIT_PROP_DATA	      "data"
#define FIT_PROP_ALGO	      "algo"
#define FIT_PROP_DATA_OFFSET  "data-offset"
#define FIT_PROP_DATA_SIZE    "data-size"
#define FIT_PROP_STORE_OFFSET "store-offset"
#define FIT_PROP_VALUE	      "value"
#define FIT_PROP_SKIP_OFFSET  "skip-offset"
#define FIT_VERSION	      "version"

static const char *const fit_err[FITE_COUNT] = {
    [FITE_BAD_HEADER] = "Bad device tree header",
    [FITE_NO_CONFIG_NODE] = "Missing /configuration node",
    [FITE_NOT_FOUND] = "Not found",
    [FITE_NO_IMAGES_NODE] = "Missing /images node",
    [FITE_MISSING_IMAGE] = "Missing image referred to by configuration",
    [FITE_MISSING_SIZE] = "Missing data-size for external data",
    [FITE_MISSING_VALUE] = "Missing value property for hash",
    [FITE_MISSING_ALGO] = "Missing algo property for hash",
    [FITE_UNKNOWN_ALGO] = "Unknown algo name",
    [FITE_INVALID_HASH_SIZE] = "Invalid hash value size",
    [FITE_HASH_MISMATCH] = "Calculated hash value does not match",
    [FITE_NEGATIVE_OFFSET] = "Image has negative store-offset or data-offset",
    [FITE_DATA_OFFSET_RANGE] = "Image data-offset is out of range of data",
    [FITE_NEGATIVE_SIZE] = "Image data-size is a negative value",
};

static const char *const fit_algo[FIT_ALGO_COUNT] = {
    [FIT_ALGO_CRC32] = "crc32",
};

int
fit_open(struct fit_info *fit, const void *buf, size_t size)
{
	int ret;

	ret = fdt_check_header(buf);
	if (ret)
		return -FITE_BAD_HEADER;
	fit->blob = buf;
	fit->size = size;

	return false;
}

void
fit_close(struct fit_info *fit)
{
}

const char *
fit_strerror(int err)
{
	if (err >= 0)
		return "no error";
	err = -err;
	if (err >= FITE_COUNT)
		return "invalid error";

	return fit_err[err];
}

static int
fit_getprop_u32(struct fit_info *fit, int node, const char *prop, int *valp)
{
	const fdt32_t *val;

	val = fdt_getprop(fit->blob, node, prop, NULL);
	if (!val)
		return -FITE_NOT_FOUND;
	*valp = fdt32_to_cpu(*val);

	return 0;
}

int
fit_first_cfg(struct fit_info *fit)
{
	int subnode, node;

	node = fdt_path_offset(fit->blob, FIT_CONFIG_PATH);
	if (node < 0)
		return -FITE_NO_CONFIG_NODE;

	subnode = fdt_first_subnode(fit->blob, node);
	if (subnode < 0)
		return -FITE_NOT_FOUND;

	return subnode;
}

int
fit_next_cfg(struct fit_info *fit, int preb_cfg)
{
	int subnode;

	subnode = fdt_next_subnode(fit->blob, preb_cfg);
	if (subnode < 0)
		return -FITE_NOT_FOUND;

	return subnode;
}

const char *
fit_cfg_name(struct fit_info *fit, int cfg)
{
	return fdt_get_name(fit->blob, cfg, NULL);
}

const char *
fit_cfg_compat_item(struct fit_info *fit, int cfg, int index)
{
	return fdt_stringlist_get(fit->blob, cfg, FIT_PROP_COMPATIBLE, index, NULL);
}

int
fit_cfg_img_count(struct fit_info *fit, int cfg, const char *prop_name)
{
	int count;

	count = fdt_stringlist_count(fit->blob, cfg, prop_name);
	if (count < 0)
		return -FITE_NOT_FOUND;

	return count;
}

int
fit_cfg_img(struct fit_info *fit, int cfg, const char *prop_name, int index)
{
	const char *name;
	int images, image;

	name = fdt_stringlist_get(fit->blob, cfg, prop_name, index, NULL);
	if (!name)
		return -FITE_NOT_FOUND;

	images = fdt_path_offset(fit->blob, FIT_IMAGE_PATH);
	if (images < 0)
		return -FITE_NO_IMAGES_NODE;

	image = fdt_subnode_offset(fit->blob, images, name);
	if (image < 0)
		return -FITE_MISSING_IMAGE;

	return image;
}

const char *
fit_cfg_version(struct fit_info *fit, int cfg)
{
	return fdt_getprop(fit->blob, cfg, FIT_VERSION, NULL);
}

const char *
fit_img_name(struct fit_info *fit, int img)
{
	return fdt_get_name(fit->blob, img, NULL);
}

static enum fit_algo_t
fit_get_algo(struct fit_info *fit, int node)
{
	const char *algo;
	int i;

	algo = fdt_getprop(fit->blob, node, FIT_PROP_ALGO, NULL);
	if (!algo)
		return -FITE_MISSING_ALGO;

	for (i = 0; i < FIT_ALGO_COUNT; i++) {
		if (!strcmp(fit_algo[i], algo))
			return i;
	}

	return -FITE_UNKNOWN_ALGO;
}

int
fit_check_hash(struct fit_info *fit, int node, const char *data, int size)
{
	int algo;
	const char *value;
	int val_size;

	value = fdt_getprop(fit->blob, node, FIT_PROP_VALUE, &val_size);
	if (!value)
		return -FITE_MISSING_VALUE;

	/* Only check the algo after we have found a value */
	algo = (int)fit_get_algo(fit, node);
	if (algo < 0)
		return algo;

	switch (algo) {
	case FIT_ALGO_CRC32: {
		unsigned long actual;
		unsigned long expect;

		if (val_size != 4)
			return -FITE_INVALID_HASH_SIZE;
		expect = fdt32_to_cpu(*(fdt32_t *)value);
		actual = crc32(0, (unsigned char *)data, size);
		if (expect != actual)
			return -FITE_HASH_MISMATCH;
		break;
	}
	default:
		return -FITE_UNKNOWN_ALGO;
	}

	return 0;
}

int
fit_check_hashes(struct fit_info *fit, int img, const char *data, int size)
{
	int node;
	int ret;

	for (node = fdt_first_subnode(fit->blob, img); node > 0;
	     node = fdt_next_subnode(fit->blob, node)) {
		if (!strncmp("hash", fdt_get_name(fit->blob, node, NULL), 4)) {
			ret = fit_check_hash(fit, node, data, size);

			/* If the value is missing, we don't check it */
			if (ret && ret != -FITE_MISSING_VALUE)
				return ret;
		}
	}

	return 0;
}

const char *
fit_img_data(struct fit_info *fit, int img, int *sizep)
{
	const char *data;
	int offset, size;

	if (!sizep)
		return NULL;
	if (!fit_getprop_u32(fit, img, FIT_PROP_DATA_OFFSET, &offset)) {
		int start;

		if (fit_getprop_u32(fit, img, FIT_PROP_DATA_SIZE, &size)) {
			*sizep = -FITE_MISSING_SIZE;
			return NULL;
		}

		if (offset < 0) {
			*sizep = -FITE_NEGATIVE_OFFSET;
			return NULL;
		}
		if (size < 0) {
			*sizep = -FITE_NEGATIVE_SIZE;
			return NULL;
		}
		start = (fdt_totalsize(fit->blob) + 3) & ~3;
		if (start + offset + size > fit->size) {
			*sizep = -FITE_DATA_OFFSET_RANGE;
			return NULL;
		}

		data = fit->blob + start + offset;
	} else {
		int ret;

		data = fdt_getprop(fit->blob, img, FIT_PROP_DATA, &size);
		if (!data) {
			*sizep = -FITE_NOT_FOUND;
			return NULL;
		}

		ret = fit_check_hashes(fit, img, data, size);
		if (ret) {
			*sizep = ret;
			return NULL;
		}
	}
	*sizep = size;

	return data;
}

int
fit_img_store_offset(struct fit_info *fit, int img)
{
	int offset;
	int ret;

	ret = fit_getprop_u32(fit, img, FIT_PROP_STORE_OFFSET, &offset);
	if (ret < 0)
		return ret;
	if (offset < 0)
		return -FITE_NEGATIVE_OFFSET;

	return offset;
}

long
fit_get_u32(const void *fdt, int node, const char *prop_name)
{
	const fdt32_t *val;
	int len;

	val = fdt_getprop(fdt, node, prop_name, &len);
	if (!val || len != sizeof(fdt32_t))
		return -1;

	return fdt32_to_cpu(*val);
}

long
fit_get_u64(const void *fdt, int node, const char *prop_name)
{
	const fdt64_t *val;
	int len;

	val = fdt_getprop(fdt, node, prop_name, &len);
	if (!val || len != sizeof(fdt64_t))
		return -1;

	return fdt64_to_cpu(*val);
}
