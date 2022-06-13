/*
 * Library for U-Boot Flat Image Tree (FIT)
 *
 * This will soon become an independent library but even then it will be some
 * years before it is considered stable enough to use as such. The intended
 * repo is https://github.com/devicetree-org/fdt-tools
 *
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_PLUGIN_VBE_FIT_H
#define __FU_PLUGIN_VBE_FIT_H

/**
 * Functions returning an error provide a negated value from this list
 *
 * @FIT_ERR_OK: Zero value indicating no error
 * @FITE_BAD_HEADER: Device tree header is not valid
 * @FITE_NO_CONFIG_NODE: The /configurations node is missing
 * @EFIT_NOT_FOUND: No (more) items found
 * @FITE_NO_IMAGES_NODE: The /images node is missing
 * @FITE_MISSING_IMAGE: An image referred to in a configuration is missing
 * @FITE_MISSING_SIZE: An external image does not have an 'image-size' property
 * @FITE_MISSING_VALUE: An image hash does not have a 'value' property
 * @FITE_MISSING_ALGO: An image hash does not have an 'algo' property
 * @FITE_UNKNOWN_ALGO: An unknown algorithm name was provided
 * @FITE_INVALID_HASH_SIZE: The hash value is not the right size for the algo
 * @FITE_HASH_MISMATCH: Hash value calculated from data contents doesn't match
 *	its value in the 'value' property
 * @FITE_NEGATIVE_OFFSET: Image store-offset or data-offset is a negative value
 *	(must be positive)
 * @FITE_DATA_OFFSET_RANGE: Image data-offset is out of range of the available
 *	data. This means that it extends past the end of the external data
 *	attached to the end of the FIT
 * @FITE_NEGATIVE_SIZE: Image data-size is a negative value (must be positive)
 */
enum fit_err_t {
	FIT_ERR_OK = 0,
	FITE_BAD_HEADER,
	FITE_NO_CONFIG_NODE,
	FITE_NOT_FOUND,
	FITE_NO_IMAGES_NODE,
	FITE_MISSING_IMAGE,
	FITE_MISSING_SIZE,
	FITE_MISSING_VALUE,
	FITE_MISSING_ALGO,
	FITE_UNKNOWN_ALGO,
	FITE_INVALID_HASH_SIZE,
	FITE_HASH_MISMATCH,
	FITE_NEGATIVE_OFFSET,
	FITE_DATA_OFFSET_RANGE,
	FITE_NEGATIVE_SIZE,

	FITE_COUNT,
};

/**
 * enum fit_algo_t - Algorithm used to hash an image
 *
 * @FIT_ALGO_CRC32: Use crc32
 */
enum fit_algo_t {
	FIT_ALGO_CRC32,

	FIT_ALGO_COUNT
};

/**
 * struct fit_info - Information about a Flat Image Tree being processed
 *
 * @blob: Pointer to FIT data (format is device tree binary / dtb)
 * @size: Size of FIT data in bytes
 */
struct fit_info {
	const char *blob;
	int size;
};

/**
 * fit_open() - Open a FIT ready for use
 *
 * The FIT must be entirely within in the buffer, but it may have external data
 * in which case this appears after the FIT.
 *
 * @fit: Place to put info about the FIT
 * @buf: Buffer containing the FIT
 * @size: Size of the buffer
 * Returns: 0 if OK, -ve fit_err_t on error
 */
int
fit_open(struct fit_info *fit, const void *buf, size_t size);

/**
 * fit_close() - Shut down a FIT after use
 *
 * This frees any memory in use
 *
 * @fit: FIT to shut down
 */
void
fit_close(struct fit_info *fit);

/**
 * fit_strerror() - Look up a FIT error number
 *
 * Since all errors are negative, this should be a negative number. If not, then
 * a placeholder string is returned
 *
 * @err: Error number (-ve value)
 * Returns: string corresponding to that error
 */
const char *
fit_strerror(int err);

/**
 * fit_first_cfg() - Find the first configuration in the FIT
 *
 * @fit: FIT to check
 * Returns: offset of first configuration, or -EFIT_NOT_FOUND if not found
 */
int
fit_first_cfg(struct fit_info *fit);

/**
 * fit_next_cfg() - Find the next configuration in the FIT
 *
 * @fit: FIT to check
 * @prev_cfg: Offset of the previous configuration
 * Returns: offset of next configuration, or -EFIT_NOT_FOUND if not found
 */
int
fit_next_cfg(struct fit_info *fit, int preb_cfg);

/**
 * fit_cfg_name() - Get the name of a configuration
 *
 * @fit: FIT to check
 * @cfg: Offset of configuration node to check
 * Returns: name of configuration, or NULL if @cfg is invalid
 */
const char *
fit_cfg_name(struct fit_info *fit, int cfg);

/**
 * fit_cfg_compat_item() - Get the name of one of a configs's compat strings
 *
 * The config has a list of compatible strings, indexed from 0. This function
 * returns am indexed string
 *
 * @fit: FIT to check
 * @cfg: Offset of configuration node to check
 * @index: Index of compatible string (0 for first, 1 for next...)
 * Returns: Configuration's compatible string with that index, or NULL if none
 */
const char *
fit_cfg_compat_item(struct fit_info *fit, int cfg, int index);

/**
 * fit_cfg_image_count() - Get the number of images in a configuration
 *
 * This returns the number of images in a particular configuration-node
 * property. For example, for:
 *
 *	firmware = "u-boot", "op-tee";
 *
 * this would return 2, since there are two images mentioned.
 *
 * @fit: FIT to check
 * @cfg: Offset of configuration node to check
 * @prop_name: Name of property to look up
 * Returns: Number of images in the configuration, or -ve if the offset is
 * invalid or the property is not found
 */
int
fit_cfg_img_count(struct fit_info *fit, int cfg, const char *prop_name);

/**
 * fit_cfg_image() - Get the offset of an image from a configuration
 *
 * Look up a particular name in a stringlist and find the image with that name.
 *
 * @fit: FIT to check
 * @cfg: Offset of configuration node to check
 * @prop_name: Name of property to look up
 * @index: Index of string to use (0=first)
 * Returns: offset of image node, or -ve on error
 */
int
fit_cfg_img(struct fit_info *fit, int cfg, const char *prop_name, int index);

/**
 * fit_cfg_version() - Get the version of a configuration
 *
 * @fit: FIT to check
 * @cfg: Offset of configuration node to check
 * Returns: configuration version, or NULL if none
 */
const char *
fit_cfg_version(struct fit_info *fit, int cfg);

/**
 * fit_img_name() - Get the name of an image
 *
 * @fit: FIT to check
 * @img: Offset of image node
 * Returns: name of the image (node name), or NULL if @offset is invalid
 */
const char *
fit_img_name(struct fit_info *fit, int img);

/**
 * fit_img_data() - Get the data from an image node
 *
 * This handles both internal and external data. It does not handle the
 * data-position property, only data-offset sinze there is no absolute memory
 * addressing available in this library.
 *
 * If any hashes are provided they are checked.
 *
 * @fit: FIT to check
 * @img: Offset of image node
 * @sizep: Returns the size of the image in bytes, if found. If not found,
 * this returns the error code. This cannot be NULL.
 * Returns: Pointer to image or NULL if not found
 */
const char *
fit_img_data(struct fit_info *fit, int img, int *sizep);

/**
 * fit_check_hash() - Check that the hash matches given data
 *
 * @fit: FIT to check
 * @node: Offset of hash node (e.g. subnode of image)
 * @data: Data to check
 * @size: Size of data to check
 * Returns: 0 if OK, -FITE_MISSING_VALUE if the value is missing,
 * -FITE_INVALID_HASH_SIZE if the hash value has an invalid size (e.g. must be
 * 4 for crc32), -FITE_HASH_MISMATCH if the hash does not match,
 * -FITE_MISSING_ALGO if there is no 'algo' property, -FITE_UNKNOWN_ALGO if the
 * algorithm is unknown
 */
int
fit_check_hash(struct fit_info *fit, int node, const char *data, int size);

/**
 * fit_check_hashes() - Check that an image's hashes match the given data
 *
 * This iterates through any hash subnodes (named 'hash...') in the image node
 * If a hash node has no value, the node is ignored.
 *
 * @fit: FIT to check
 * @img: Offset of image node
 * @data: Data to check
 * @size: Size of data to check
 * Returns: 0 if OK, -FITE_INVALID_HASH_SIZE if the hash value has an invalid
 * size (e.g. must be 4 for crc32), -FITE_HASH_MISMATCH if the hash does not
 * match, -FITE_MISSING_ALGO if there is no 'algo' property, -FITE_UNKNOWN_ALGO
 * if the algorithm is unknown
 */
int
fit_check_hashes(struct fit_info *fit, int img, const char *data, int size);

/**
 * fit_img_offset() - Get the store offset for an image
 *
 * The image can be placed at a particular offset in the firmware region. This
 * reads that offset.
 *
 * @fit: FIT to check
 * @img: Offset of image node
 * Returns: store offset, if found
 * Returns: offset, on success, -FITE_NOT_FOUND if there is no offset
 */
int
fit_img_store_offset(struct fit_info *fit, int img);

/**
 * fit_img_skip_offset() - Get the skip offset for an image
 *
 * This allows an initial part of the image to be skipped when writing. This
 * means that the first part of the image is ignored, with just the latter part
 * being written. For example, if this is 0x200 then the first 512 bytes of the
 * image (which must be present in the image) are skipped and the bytes after
 * that are written to the store offset.
 *
 * @fit: FIT to check
 * @img: Offset of image node
 * Returns: offset, if found
 * Returns: offset, on success, -FITE_NOT_FOUND if there is no offset
 */
int
fit_img_skip_offset(struct fit_info *fit, int img);

/**
 * fit_get_u32() - Get a 32-bit integer value from the device tree
 *
 * @fdt: Device tree to read from
 * @node: Node offset to read from
 * @prop_name: Name of property to read
 * Returns: integer value, if found and of the correct size, else -1
 */
long
fit_get_u32(const void *fdt, int node, const char *prop_name);

/**
 * fit_get_u64() - Get a 64-bit integer value from the device tree
 *
 * @fdt: Device tree to read from
 * @node: Node offset to read from
 * @prop_name: Name of property to read
 * Returns: integer value, if found and of the correct size, else -1
 */
long
fit_get_u64(const void *fdt, int node, const char *prop_name);

#endif /* __FU_PLUGIN_VBE_FIT_H */
