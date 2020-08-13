/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efi-signature.h"

gboolean	 fu_efi_signature_list_array_inclusive	(GPtrArray	*outer,
							 GPtrArray	*inner);
