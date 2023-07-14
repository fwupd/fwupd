/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-cfu-firmware-struct.h"
#include "fu-cfu-offer.h"
#include "fu-common.h"
#include "fu-string.h"

/**
 * FuCfuOffer:
 *
 * A CFU offer. This is a 16 byte blob which contains enough data for the device to either accept
 * or refuse a firmware payload. The offer may be loaded from disk, network, or even constructed
 * manually. There is much left to how the specific firmware implements CFU, and it's expected
 * that multiple different plugins will use this offer in different ways.
 *
 * Documented: https://docs.microsoft.com/en-us/windows-hardware/drivers/cfu/cfu-specification
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint8 segment_number;
	gboolean force_immediate_reset;
	gboolean force_ignore_version;
	guint8 component_id;
	guint8 token;
	guint32 hw_variant;
	guint8 protocol_revision;
	guint8 bank;
	guint8 milestone;
	guint16 product_id;
} FuCfuOfferPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCfuOffer, fu_cfu_offer, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_cfu_offer_get_instance_private(o))

static void
fu_cfu_offer_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCfuOffer *self = FU_CFU_OFFER(firmware);
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "segment_number", priv->segment_number);
	fu_xmlb_builder_insert_kb(bn, "force_immediate_reset", priv->force_immediate_reset);
	fu_xmlb_builder_insert_kb(bn, "force_ignore_version", priv->force_ignore_version);
	fu_xmlb_builder_insert_kx(bn, "component_id", priv->component_id);
	fu_xmlb_builder_insert_kx(bn, "token", priv->token);
	fu_xmlb_builder_insert_kx(bn, "hw_variant", priv->hw_variant);
	fu_xmlb_builder_insert_kx(bn, "protocol_revision", priv->protocol_revision);
	fu_xmlb_builder_insert_kx(bn, "bank", priv->bank);
	fu_xmlb_builder_insert_kx(bn, "milestone", priv->milestone);
	fu_xmlb_builder_insert_kx(bn, "product_id", priv->product_id);
}

/**
 * fu_cfu_offer_get_segment_number:
 * @self: a #FuCfuOffer
 *
 * Gets the part of the firmware that is being transferred.
 *
 * Returns: integer
 *
 * Since: 1.7.0
 **/
guint8
fu_cfu_offer_get_segment_number(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), 0x0);
	return priv->segment_number;
}

/**
 * fu_cfu_offer_get_force_immediate_reset:
 * @self: a #FuCfuOffer
 *
 * Gets if the in-situ firmware should reset into the new firmware immediately, rather than waiting
 * for the next time the device is replugged.
 *
 * Returns: boolean
 *
 * Since: 1.7.0
 **/
gboolean
fu_cfu_offer_get_force_immediate_reset(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), FALSE);
	return priv->force_immediate_reset;
}

/**
 * fu_cfu_offer_get_force_ignore_version:
 * @self: a #FuCfuOffer
 *
 * Gets if the in-situ firmware should ignore version mismatch (e.g. downgrade).
 *
 * Returns: boolean
 *
 * Since: 1.7.0
 **/
gboolean
fu_cfu_offer_get_force_ignore_version(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), FALSE);
	return priv->force_ignore_version;
}

/**
 * fu_cfu_offer_get_component_id:
 * @self: a #FuCfuOffer
 *
 * Gets the component in the device to apply the firmware update.
 *
 * Returns: integer
 *
 * Since: 1.7.0
 **/
guint8
fu_cfu_offer_get_component_id(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), 0x0);
	return priv->component_id;
}

/**
 * fu_cfu_offer_get_token:
 * @self: a #FuCfuOffer
 *
 * Gets the token to identify the user specific software making the offer.
 *
 * Returns: integer
 *
 * Since: 1.7.0
 **/
guint8
fu_cfu_offer_get_token(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), 0x0);
	return priv->token;
}

/**
 * fu_cfu_offer_get_hw_variant:
 * @self: a #FuCfuOffer
 *
 * Gets the hardware variant bitmask corresponding with compatible firmware.
 *
 * Returns: integer
 *
 * Since: 1.7.0
 **/
guint32
fu_cfu_offer_get_hw_variant(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), 0x0);
	return priv->hw_variant;
}

/**
 * fu_cfu_offer_get_protocol_revision:
 * @self: a #FuCfuOffer
 *
 * Gets the CFU protocol version.
 *
 * Returns: integer
 *
 * Since: 1.7.0
 **/
guint8
fu_cfu_offer_get_protocol_revision(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), 0x0);
	return priv->protocol_revision;
}

/**
 * fu_cfu_offer_get_bank:
 * @self: a #FuCfuOffer
 *
 * Gets the bank register, used if multiple banks are supported.
 *
 * Returns: integer
 *
 * Since: 1.7.0
 **/
guint8
fu_cfu_offer_get_bank(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), 0x0);
	return priv->bank;
}

/**
 * fu_cfu_offer_get_milestone:
 * @self: a #FuCfuOffer
 *
 * Gets the milestone, which can be used as a version for example EV1, EVT etc.
 *
 * Returns: integer
 *
 * Since: 1.7.0
 **/
guint8
fu_cfu_offer_get_milestone(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), 0x0);
	return priv->milestone;
}

/**
 * fu_cfu_offer_get_product_id:
 * @self: a #FuCfuOffer
 *
 * Gets the product ID for this CFU image.
 *
 * Returns: integer
 *
 * Since: 1.7.0
 **/
guint16
fu_cfu_offer_get_product_id(FuCfuOffer *self)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFU_OFFER(self), 0x0);
	return priv->product_id;
}

/**
 * fu_cfu_offer_set_segment_number:
 * @self: a #FuCfuOffer
 * @segment_number: integer
 *
 * Sets the part of the firmware that is being transferred.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_segment_number(FuCfuOffer *self, guint8 segment_number)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	priv->segment_number = segment_number;
}

/**
 * fu_cfu_offer_set_force_immediate_reset:
 * @self: a #FuCfuOffer
 * @force_immediate_reset: boolean
 *
 * Sets if the in-situ firmware should reset into the new firmware immediately, rather than waiting
 * for the next time the device is replugged.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_force_immediate_reset(FuCfuOffer *self, gboolean force_immediate_reset)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	priv->force_immediate_reset = force_immediate_reset;
}

/**
 * fu_cfu_offer_set_force_ignore_version:
 * @self: a #FuCfuOffer
 * @force_ignore_version: boolean
 *
 * Sets if the in-situ firmware should ignore version mismatch (e.g. downgrade).
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_force_ignore_version(FuCfuOffer *self, gboolean force_ignore_version)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	priv->force_ignore_version = force_ignore_version;
}

/**
 * fu_cfu_offer_set_component_id:
 * @self: a #FuCfuOffer
 * @component_id: integer
 *
 * Sets the component in the device to apply the firmware update.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_component_id(FuCfuOffer *self, guint8 component_id)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	priv->component_id = component_id;
}

/**
 * fu_cfu_offer_set_token:
 * @self: a #FuCfuOffer
 * @token: integer
 *
 * Sets the token to identify the user specific software making the offer.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_token(FuCfuOffer *self, guint8 token)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	priv->token = token;
}

/**
 * fu_cfu_offer_set_hw_variant:
 * @self: a #FuCfuOffer
 * @hw_variant: integer
 *
 * Sets the hardware variant bitmask corresponding with compatible firmware.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_hw_variant(FuCfuOffer *self, guint32 hw_variant)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	priv->hw_variant = hw_variant;
}

/**
 * fu_cfu_offer_set_protocol_revision:
 * @self: a #FuCfuOffer
 * @protocol_revision: integer
 *
 * Sets the CFU protocol version.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_protocol_revision(FuCfuOffer *self, guint8 protocol_revision)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	g_return_if_fail(protocol_revision <= 0b1111);
	priv->protocol_revision = protocol_revision;
}

/**
 * fu_cfu_offer_set_bank:
 * @self: a #FuCfuOffer
 * @bank: integer
 *
 * Sets bank register, used if multiple banks are supported.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_bank(FuCfuOffer *self, guint8 bank)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	g_return_if_fail(bank <= 0b11);
	priv->bank = bank;
}

/**
 * fu_cfu_offer_set_milestone:
 * @self: a #FuCfuOffer
 * @milestone: integer
 *
 * Sets the milestone, which can be used as a version for example EV1, EVT etc.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_milestone(FuCfuOffer *self, guint8 milestone)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	g_return_if_fail(milestone <= 0b111);
	priv->milestone = milestone;
}

/**
 * fu_cfu_offer_set_product_id:
 * @self: a #FuCfuOffer
 * @product_id: integer
 *
 * Sets the product ID for this CFU image.
 *
 * Since: 1.7.0
 **/
void
fu_cfu_offer_set_product_id(FuCfuOffer *self, guint16 product_id)
{
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFU_OFFER(self));
	priv->product_id = product_id;
}

static gboolean
fu_cfu_offer_parse(FuFirmware *firmware,
		   GBytes *fw,
		   gsize offset,
		   FwupdInstallFlags flags,
		   GError **error)
{
	FuCfuOffer *self = FU_CFU_OFFER(firmware);
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	gsize bufsz = 0;
	guint8 flags1;
	guint8 flags2;
	guint8 flags3;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st = NULL;

	/* parse */
	st = fu_struct_cfu_offer_parse(buf, bufsz, offset, error);
	if (st == NULL)
		return FALSE;
	priv->segment_number = fu_struct_cfu_offer_get_segment_number(st);
	priv->component_id = fu_struct_cfu_offer_get_component_id(st);
	priv->token = fu_struct_cfu_offer_get_token(st);
	priv->hw_variant = fu_struct_cfu_offer_get_compat_variant_mask(st);
	priv->product_id = fu_struct_cfu_offer_get_product_id(st);
	fu_firmware_set_version_raw(firmware, fu_struct_cfu_offer_get_version(st));

	/* component info */
	flags1 = fu_struct_cfu_offer_get_flags1(st);
	priv->force_ignore_version = (flags1 & 0b10000000) > 0;
	priv->force_immediate_reset = (flags1 & 0b01000000) > 0;

	/* product info */
	flags2 = fu_struct_cfu_offer_get_flags2(st);
	priv->protocol_revision = (flags2 >> 4) & 0b1111;
	priv->bank = (flags2 >> 2) & 0b11;
	flags3 = fu_struct_cfu_offer_get_flags3(st);
	priv->milestone = (flags3 >> 5) & 0b111;

	/* success */
	return TRUE;
}

static GByteArray *
fu_cfu_offer_write(FuFirmware *firmware, GError **error)
{
	FuCfuOffer *self = FU_CFU_OFFER(firmware);
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) st = fu_struct_cfu_offer_new();

	/* component info */
	fu_struct_cfu_offer_set_segment_number(st, priv->segment_number);
	fu_struct_cfu_offer_set_flags1(st,
				       priv->force_ignore_version << 7 |
					   (priv->force_immediate_reset << 6));
	fu_struct_cfu_offer_set_component_id(st, priv->component_id);
	fu_struct_cfu_offer_set_token(st, priv->token);

	/* version */
	fu_struct_cfu_offer_set_version(st, fu_firmware_get_version_raw(firmware));
	fu_struct_cfu_offer_set_compat_variant_mask(st, priv->hw_variant);

	/* product info */
	fu_struct_cfu_offer_set_flags2(st, (priv->protocol_revision << 4) | (priv->bank << 2));
	fu_struct_cfu_offer_set_flags3(st, priv->milestone << 5);
	fu_struct_cfu_offer_set_product_id(st, priv->product_id);

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_cfu_offer_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuCfuOffer *self = FU_CFU_OFFER(firmware);
	FuCfuOfferPrivate *priv = GET_PRIVATE(self);
	guint64 tmp;
	const gchar *tmpb;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "segment_number", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->segment_number = tmp;
	tmpb = xb_node_query_text(n, "force_immediate_reset", NULL);
	if (tmpb != NULL) {
		if (!fu_strtobool(tmpb, &priv->force_immediate_reset, error))
			return FALSE;
	}
	tmpb = xb_node_query_text(n, "force_ignore_version", NULL);
	if (tmpb != NULL) {
		if (!fu_strtobool(tmpb, &priv->force_ignore_version, error))
			return FALSE;
	}
	tmp = xb_node_query_text_as_uint(n, "component_id", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->component_id = tmp;
	tmp = xb_node_query_text_as_uint(n, "token", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->token = tmp;
	tmp = xb_node_query_text_as_uint(n, "hw_variant", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->hw_variant = tmp;
	tmp = xb_node_query_text_as_uint(n, "protocol_revision", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->protocol_revision = tmp;
	tmp = xb_node_query_text_as_uint(n, "bank", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->bank = tmp;
	tmp = xb_node_query_text_as_uint(n, "milestone", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->milestone = tmp;
	tmp = xb_node_query_text_as_uint(n, "product_id", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		priv->product_id = tmp;

	/* success */
	return TRUE;
}

static void
fu_cfu_offer_init(FuCfuOffer *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_cfu_offer_class_init(FuCfuOfferClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_cfu_offer_export;
	klass_firmware->parse = fu_cfu_offer_parse;
	klass_firmware->write = fu_cfu_offer_write;
	klass_firmware->build = fu_cfu_offer_build;
}

/**
 * fu_cfu_offer_new:
 *
 * Creates a new #FuFirmware for a CFU offer
 *
 * Since: 1.7.0
 **/
FuFirmware *
fu_cfu_offer_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_CFU_OFFER, NULL));
}
