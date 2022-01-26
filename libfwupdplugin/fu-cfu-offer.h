/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_CFU_OFFER (fu_cfu_offer_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCfuOffer, fu_cfu_offer, FU, CFU_OFFER, FuFirmware)

struct _FuCfuOfferClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_cfu_offer_new(void);
guint8
fu_cfu_offer_get_segment_number(FuCfuOffer *self);
gboolean
fu_cfu_offer_get_force_immediate_reset(FuCfuOffer *self);
gboolean
fu_cfu_offer_get_force_ignore_version(FuCfuOffer *self);
guint8
fu_cfu_offer_get_component_id(FuCfuOffer *self);
guint8
fu_cfu_offer_get_token(FuCfuOffer *self);
guint32
fu_cfu_offer_get_hw_variant(FuCfuOffer *self);
guint8
fu_cfu_offer_get_protocol_revision(FuCfuOffer *self);
guint8
fu_cfu_offer_get_bank(FuCfuOffer *self);
guint8
fu_cfu_offer_get_milestone(FuCfuOffer *self);
guint16
fu_cfu_offer_get_product_id(FuCfuOffer *self);

void
fu_cfu_offer_set_segment_number(FuCfuOffer *self, guint8 segment_number);
void
fu_cfu_offer_set_force_immediate_reset(FuCfuOffer *self, gboolean force_immediate_reset);
void
fu_cfu_offer_set_force_ignore_version(FuCfuOffer *self, gboolean force_ignore_version);
void
fu_cfu_offer_set_component_id(FuCfuOffer *self, guint8 component_id);
void
fu_cfu_offer_set_token(FuCfuOffer *self, guint8 token);
void
fu_cfu_offer_set_hw_variant(FuCfuOffer *self, guint32 hw_variant);
void
fu_cfu_offer_set_protocol_revision(FuCfuOffer *self, guint8 protocol_revision);
void
fu_cfu_offer_set_bank(FuCfuOffer *self, guint8 bank);
void
fu_cfu_offer_set_milestone(FuCfuOffer *self, guint8 milestone);
void
fu_cfu_offer_set_product_id(FuCfuOffer *self, guint16 product_id);
