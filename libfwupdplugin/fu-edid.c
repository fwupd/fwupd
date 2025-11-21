/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEdid"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-edid-struct.h"
#include "fu-edid.h"
#include "fu-string.h"

struct _FuEdid {
	FuFirmware parent_instance;
	gchar *pnp_id;
	gchar *serial_number;
	gchar *product_name;
	gchar *eisa_id;
	guint16 product_code;
};

G_DEFINE_TYPE(FuEdid, fu_edid, FU_TYPE_FIRMWARE)

/**
 * fu_edid_get_pnp_id:
 * @self: a #FuEdid
 *
 * Gets the PNP ID, e.g. `IBM`.
 *
 * Returns: string value, or %NULL for unset
 *
 * Since: 1.9.6
 **/
const gchar *
fu_edid_get_pnp_id(FuEdid *self)
{
	g_return_val_if_fail(FU_IS_EDID(self), NULL);
	return self->pnp_id;
}

/**
 * fu_edid_set_pnp_id:
 * @self: a #FuEdid
 * @pnp_id: (nullable): three digit string value, or %NULL
 *
 * Sets the PNP ID, which has a length equal to or less than 3 ASCII characters.
 *
 * Since: 1.9.6
 **/
void
fu_edid_set_pnp_id(FuEdid *self, const gchar *pnp_id)
{
	g_return_if_fail(FU_IS_EDID(self));
	if (g_strcmp0(self->pnp_id, pnp_id) == 0)
		return;
	g_free(self->pnp_id);
	self->pnp_id = g_strdup(pnp_id);
}

/**
 * fu_edid_get_eisa_id:
 * @self: a #FuEdid
 *
 * Gets the EISA ID, e.g. `LTN154P2-L05`.
 *
 * Returns: string value, or %NULL for unset
 *
 * Since: 1.9.6
 **/
const gchar *
fu_edid_get_eisa_id(FuEdid *self)
{
	g_return_val_if_fail(FU_IS_EDID(self), NULL);
	return self->eisa_id;
}

/**
 * fu_edid_set_eisa_id:
 * @self: a #FuEdid
 * @eisa_id: (nullable): string value, or %NULL
 *
 * Sets the EISA ID, which has to be equal to or less than 13 ASCII characters long.
 *
 * Since: 1.9.6
 **/
void
fu_edid_set_eisa_id(FuEdid *self, const gchar *eisa_id)
{
	g_return_if_fail(FU_IS_EDID(self));
	if (g_strcmp0(self->eisa_id, eisa_id) == 0)
		return;
	g_free(self->eisa_id);
	self->eisa_id = g_strdup(eisa_id);
}

/**
 * fu_edid_get_serial_number:
 * @self: a #FuEdid
 *
 * Gets the serial number.
 *
 * Returns: string value, or %NULL for unset
 *
 * Since: 1.9.6
 **/
const gchar *
fu_edid_get_serial_number(FuEdid *self)
{
	g_return_val_if_fail(FU_IS_EDID(self), NULL);
	return self->serial_number;
}

/**
 * fu_edid_set_serial_number:
 * @self: a #FuEdid
 * @serial_number: (nullable): string value, or %NULL
 *
 * Sets the serial number, which can either be a unsigned 32 bit integer, or a string equal to or
 * less than 13 ASCII characters long.
 *
 * Since: 1.9.6
 **/
void
fu_edid_set_serial_number(FuEdid *self, const gchar *serial_number)
{
	g_return_if_fail(FU_IS_EDID(self));
	if (g_strcmp0(self->serial_number, serial_number) == 0)
		return;
	g_free(self->serial_number);
	self->serial_number = g_strdup(serial_number);
}

/**
 * fu_edid_get_product_code:
 * @self: a #FuEdid
 *
 * Gets the product code.
 *
 * Returns: integer, or 0x0 for unset
 *
 * Since: 1.9.6
 **/
guint16
fu_edid_get_product_code(FuEdid *self)
{
	g_return_val_if_fail(FU_IS_EDID(self), G_MAXUINT16);
	return self->product_code;
}

/**
 * fu_edid_set_product_code:
 * @self: a #FuEdid
 * @product_code: integer, or 0x0 for unset
 *
 * Sets the product code.
 *
 * Since: 1.9.6
 **/
void
fu_edid_set_product_code(FuEdid *self, guint16 product_code)
{
	g_return_if_fail(FU_IS_EDID(self));
	self->product_code = product_code;
}

/* return as soon as the first non-printable char is encountered */
static gchar *
fu_edid_strsafe(const guint8 *buf, gsize bufsz)
{
	g_autoptr(GString) str = g_string_new(NULL);
	for (gsize i = 0; i < bufsz; i++) {
		if (!g_ascii_isprint((gchar)buf[i]))
			break;
		g_string_append_c(str, (gchar)buf[i]);
	}
	if (str->len == 0)
		return NULL;
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static gboolean
fu_edid_parse_descriptor(FuEdid *self, GInputStream *stream, gsize offset, GError **error)
{
	gsize buf2sz = 0;
	const guint8 *buf2;
	g_autoptr(GByteArray) st = NULL;

	st = fu_struct_edid_descriptor_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;

	/* ignore pixel clock data */
	if (fu_struct_edid_descriptor_get_kind(st) != 0x0 ||
	    fu_struct_edid_descriptor_get_subkind(st) != 0x0)
		return TRUE;

	buf2 = fu_struct_edid_descriptor_get_data(st, &buf2sz);
	if (fu_struct_edid_descriptor_get_tag(st) == FU_EDID_DESCRIPTOR_TAG_DISPLAY_PRODUCT_NAME) {
		g_free(self->product_name);
		self->product_name = fu_edid_strsafe(buf2, buf2sz);
	} else if (fu_struct_edid_descriptor_get_tag(st) ==
		   FU_EDID_DESCRIPTOR_TAG_DISPLAY_PRODUCT_SERIAL_NUMBER) {
		g_free(self->serial_number);
		self->serial_number = fu_edid_strsafe(buf2, buf2sz);
	} else if (fu_struct_edid_descriptor_get_tag(st) ==
		   FU_EDID_DESCRIPTOR_TAG_ALPHANUMERIC_DATA_STRING) {
		g_free(self->eisa_id);
		self->eisa_id = fu_edid_strsafe(buf2, buf2sz);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_edid_parse(FuFirmware *firmware,
	      GInputStream *stream,
	      FuFirmwareParseFlags flags,
	      GError **error)
{
	FuEdid *self = FU_EDID(firmware);
	const guint8 *manu_id;
	gsize offset = 0;
	g_autofree gchar *pnp_id = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* parse header */
	st = fu_struct_edid_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;

	/* decode the PNP ID from three 5 bit words packed into 2 bytes
	 * /--00--\/--01--\
	 * 7654321076543210
	 * |\---/\---/\---/
	 * R  C1   C2   C3 */
	manu_id = fu_struct_edid_get_manufacturer_name(st, NULL);
	pnp_id = g_strdup_printf(
	    "%c%c%c",
	    'A' + ((manu_id[0] & 0b01111100) >> 2) - 1,
	    'A' + (((manu_id[0] & 0b00000011) << 3) + ((manu_id[1] & 0b11100000) >> 5)) - 1,
	    'A' + (manu_id[1] & 0b00011111) - 1);
	fu_edid_set_pnp_id(self, pnp_id);
	fu_edid_set_product_code(self, fu_struct_edid_get_product_code(st));
	if (fu_struct_edid_get_serial_number(st) != 0x0) {
		g_autofree gchar *serial_number =
		    g_strdup_printf("%u", fu_struct_edid_get_serial_number(st));
		fu_edid_set_serial_number(self, serial_number);
	}

	/* parse 4x18 byte sections */
	offset += FU_STRUCT_EDID_OFFSET_DATA_BLOCKS;
	for (guint i = 0; i < 4; i++) {
		if (!fu_edid_parse_descriptor(self, stream, offset, error))
			return FALSE;
		offset += FU_STRUCT_EDID_DESCRIPTOR_SIZE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_edid_write(FuFirmware *firmware, GError **error)
{
	FuEdid *self = FU_EDID(firmware);
	g_autoptr(GByteArray) st = fu_struct_edid_new();
	guint64 serial_number = 0;
	gsize offset_desc = FU_STRUCT_EDID_OFFSET_DATA_BLOCKS;

	fu_struct_edid_set_product_code(st, self->product_code);

	/* if this is a integer, store it in the header rather than in a descriptor */
	if (fu_strtoull(self->serial_number,
			&serial_number,
			0,
			G_MAXUINT32,
			FU_INTEGER_BASE_AUTO,
			NULL))
		fu_struct_edid_set_serial_number(st, serial_number);

	/* store descriptors */
	if (self->product_name != NULL) {
		g_autoptr(GByteArray) st_desc = fu_struct_edid_descriptor_new();
		fu_struct_edid_descriptor_set_tag(st_desc,
						  FU_EDID_DESCRIPTOR_TAG_DISPLAY_PRODUCT_NAME);
		if (!fu_struct_edid_descriptor_set_data(st_desc,
							(const guint8 *)self->product_name,
							strlen(self->product_name),
							error)) {
			g_prefix_error(error, "cannot write product name: ");
			return NULL;
		}
		memcpy(st->data + offset_desc, st_desc->data, st_desc->len); /* nocheck:blocked */
		offset_desc += st_desc->len;
	}
	if (self->serial_number != NULL) {
		g_autoptr(GByteArray) st_desc = fu_struct_edid_descriptor_new();
		fu_struct_edid_descriptor_set_tag(
		    st_desc,
		    FU_EDID_DESCRIPTOR_TAG_DISPLAY_PRODUCT_SERIAL_NUMBER);
		if (!fu_struct_edid_descriptor_set_data(st_desc,
							(const guint8 *)self->serial_number,
							strlen(self->serial_number),
							error)) {
			g_prefix_error(error, "cannot write serial number: ");
			return NULL;
		}
		memcpy(st->data + offset_desc, st_desc->data, st_desc->len); /* nocheck:blocked */
		offset_desc += st_desc->len;
	}
	if (self->eisa_id != NULL) {
		g_autoptr(GByteArray) st_desc = fu_struct_edid_descriptor_new();
		fu_struct_edid_descriptor_set_tag(st_desc,
						  FU_EDID_DESCRIPTOR_TAG_ALPHANUMERIC_DATA_STRING);
		if (!fu_struct_edid_descriptor_set_data(st_desc,
							(const guint8 *)self->eisa_id,
							strlen(self->eisa_id),
							error)) {
			g_prefix_error(error, "cannot write EISA ID: ");
			return NULL;
		}
		memcpy(st->data + offset_desc, st_desc->data, st_desc->len); /* nocheck:blocked */
		offset_desc += st_desc->len;
	}

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_edid_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEdid *self = FU_EDID(firmware);
	const gchar *value;

	/* simple properties */
	value = xb_node_query_text(n, "pnp_id", NULL);
	if (value != NULL) {
		gsize valuesz = strlen(value);
		if (valuesz != 3) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "pnp_id not supported, %u of %u bytes",
				    (guint)valuesz,
				    (guint)3);
			return FALSE;
		}
		fu_edid_set_pnp_id(self, value);
	}
	value = xb_node_query_text(n, "serial_number", NULL);
	if (value != NULL) {
		gsize valuesz = strlen(value);
		if (valuesz > FU_STRUCT_EDID_DESCRIPTOR_SIZE_DATA) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "serial_number not supported, %u of %u bytes",
				    (guint)valuesz,
				    (guint)FU_STRUCT_EDID_DESCRIPTOR_SIZE_DATA);
			return FALSE;
		}
		fu_edid_set_serial_number(self, value);
	}
	value = xb_node_query_text(n, "eisa_id", NULL);
	if (value != NULL) {
		gsize valuesz = strlen(value);
		if (valuesz > FU_STRUCT_EDID_DESCRIPTOR_SIZE_DATA) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "eisa_id not supported, %u of %u bytes",
				    (guint)valuesz,
				    (guint)FU_STRUCT_EDID_DESCRIPTOR_SIZE_DATA);
			return FALSE;
		}
		fu_edid_set_eisa_id(self, value);
	}
	value = xb_node_query_text(n, "product_code", NULL);
	if (value != NULL) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_edid_set_product_code(self, tmp);
	}

	/* success */
	return TRUE;
}

static void
fu_edid_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEdid *self = FU_EDID(firmware);
	fu_xmlb_builder_insert_kv(bn, "pnp_id", self->pnp_id);
	fu_xmlb_builder_insert_kv(bn, "serial_number", self->serial_number);
	fu_xmlb_builder_insert_kv(bn, "product_name", self->product_name);
	fu_xmlb_builder_insert_kv(bn, "eisa_id", self->eisa_id);
	fu_xmlb_builder_insert_kx(bn, "product_code", self->product_code);
}

static void
fu_edid_finalize(GObject *obj)
{
	FuEdid *self = FU_EDID(obj);
	g_free(self->pnp_id);
	g_free(self->serial_number);
	g_free(self->product_name);
	g_free(self->eisa_id);
	G_OBJECT_CLASS(fu_edid_parent_class)->finalize(obj);
}

static void
fu_edid_class_init(FuEdidClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_edid_finalize;
	firmware_class->parse = fu_edid_parse;
	firmware_class->write = fu_edid_write;
	firmware_class->build = fu_edid_build;
	firmware_class->export = fu_edid_export;
}

static void
fu_edid_init(FuEdid *self)
{
}

/**
 * fu_edid_new:
 *
 * Returns: (transfer full): a #FuEdid
 *
 * Since: 1.9.6
 **/
FuEdid *
fu_edid_new(void)
{
	return g_object_new(FU_TYPE_EDID, NULL);
}
