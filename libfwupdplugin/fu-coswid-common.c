/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-coswid-common.h"

FuCoswidEntityRole
fu_coswid_entity_role_from_string(const gchar *val)
{
	if (g_strcmp0(val, "tag-creator") == 0)
		return FU_COSWID_ENTITY_ROLE_TAG_CREATOR;
	if (g_strcmp0(val, "software-creator") == 0)
		return FU_COSWID_ENTITY_ROLE_SOFTWARE_CREATOR;
	if (g_strcmp0(val, "aggregator") == 0)
		return FU_COSWID_ENTITY_ROLE_AGGREGATOR;
	if (g_strcmp0(val, "distributor") == 0)
		return FU_COSWID_ENTITY_ROLE_DISTRIBUTOR;
	if (g_strcmp0(val, "licensor") == 0)
		return FU_COSWID_ENTITY_ROLE_LICENSOR;
	if (g_strcmp0(val, "maintainer") == 0)
		return FU_COSWID_ENTITY_ROLE_MAINTAINER;
	return FU_COSWID_ENTITY_ROLE_UNKNOWN;
}

const gchar *
fu_coswid_entity_role_to_string(FuCoswidEntityRole val)
{
	if (val == FU_COSWID_ENTITY_ROLE_TAG_CREATOR)
		return "tag-creator";
	if (val == FU_COSWID_ENTITY_ROLE_SOFTWARE_CREATOR)
		return "software-creator";
	if (val == FU_COSWID_ENTITY_ROLE_AGGREGATOR)
		return "aggregator";
	if (val == FU_COSWID_ENTITY_ROLE_DISTRIBUTOR)
		return "distributor";
	if (val == FU_COSWID_ENTITY_ROLE_LICENSOR)
		return "licensor";
	if (val == FU_COSWID_ENTITY_ROLE_MAINTAINER)
		return "maintainer";
	return NULL;
}

FuCoswidLinkRel
fu_coswid_link_rel_from_string(const gchar *val)
{
	if (g_strcmp0(val, "license") == 0)
		return FU_COSWID_LINK_REL_LICENSE;
	if (g_strcmp0(val, "compiler") == 0)
		return FU_COSWID_LINK_REL_COMPILER;
	if (g_strcmp0(val, "ancestor") == 0)
		return FU_COSWID_LINK_REL_ANCESTOR;
	if (g_strcmp0(val, "component") == 0)
		return FU_COSWID_LINK_REL_COMPONENT;
	if (g_strcmp0(val, "feature") == 0)
		return FU_COSWID_LINK_REL_FEATURE;
	if (g_strcmp0(val, "installationmedia") == 0)
		return FU_COSWID_LINK_REL_INSTALLATIONMEDIA;
	if (g_strcmp0(val, "packageinstaller") == 0)
		return FU_COSWID_LINK_REL_PACKAGEINSTALLER;
	if (g_strcmp0(val, "parent") == 0)
		return FU_COSWID_LINK_REL_PARENT;
	if (g_strcmp0(val, "patches") == 0)
		return FU_COSWID_LINK_REL_PATCHES;
	if (g_strcmp0(val, "requires") == 0)
		return FU_COSWID_LINK_REL_REQUIRES;
	if (g_strcmp0(val, "see-also") == 0)
		return FU_COSWID_LINK_REL_SEE_ALSO;
	if (g_strcmp0(val, "supersedes") == 0)
		return FU_COSWID_LINK_REL_SUPERSEDES;
	if (g_strcmp0(val, "supplemental") == 0)
		return FU_COSWID_LINK_REL_SUPPLEMENTAL;
	return FU_COSWID_LINK_REL_UNKNOWN;
}

const gchar *
fu_coswid_link_rel_to_string(FuCoswidLinkRel val)
{
	if (val == FU_COSWID_LINK_REL_LICENSE)
		return "license";
	if (val == FU_COSWID_LINK_REL_COMPILER)
		return "compiler";
	if (val == FU_COSWID_LINK_REL_ANCESTOR)
		return "ancestor";
	if (val == FU_COSWID_LINK_REL_COMPONENT)
		return "component";
	if (val == FU_COSWID_LINK_REL_FEATURE)
		return "feature";
	if (val == FU_COSWID_LINK_REL_INSTALLATIONMEDIA)
		return "installationmedia";
	if (val == FU_COSWID_LINK_REL_PACKAGEINSTALLER)
		return "packageinstaller";
	if (val == FU_COSWID_LINK_REL_PARENT)
		return "parent";
	if (val == FU_COSWID_LINK_REL_PATCHES)
		return "patches";
	if (val == FU_COSWID_LINK_REL_REQUIRES)
		return "requires";
	if (val == FU_COSWID_LINK_REL_SEE_ALSO)
		return "see-also";
	if (val == FU_COSWID_LINK_REL_SUPERSEDES)
		return "supersedes";
	if (val == FU_COSWID_LINK_REL_SUPPLEMENTAL)
		return "supplemental";
	return NULL;
}

FuCoswidVersionScheme
fu_coswid_version_scheme_from_string(const gchar *val)
{
	if (g_strcmp0(val, "multipartnumeric") == 0)
		return FU_COSWID_VERSION_SCHEME_MULTIPARTNUMERIC;
	if (g_strcmp0(val, "multipartnumeric-suffix") == 0)
		return FU_COSWID_VERSION_SCHEME_MULTIPARTNUMERIC_SUFFIX;
	if (g_strcmp0(val, "alphanumeric") == 0)
		return FU_COSWID_VERSION_SCHEME_ALPHANUMERIC;
	if (g_strcmp0(val, "decimal") == 0)
		return FU_COSWID_VERSION_SCHEME_DECIMAL;
	if (g_strcmp0(val, "semver") == 0)
		return FU_COSWID_VERSION_SCHEME_SEMVER;
	return FU_COSWID_VERSION_SCHEME_UNKNOWN;
}

const gchar *
fu_coswid_version_scheme_to_string(FuCoswidVersionScheme val)
{
	if (val == FU_COSWID_VERSION_SCHEME_MULTIPARTNUMERIC)
		return "multipartnumeric";
	if (val == FU_COSWID_VERSION_SCHEME_MULTIPARTNUMERIC_SUFFIX)
		return "multipartnumeric-suffix";
	if (val == FU_COSWID_VERSION_SCHEME_ALPHANUMERIC)
		return "alphanumeric";
	if (val == FU_COSWID_VERSION_SCHEME_DECIMAL)
		return "decimal";
	if (val == FU_COSWID_VERSION_SCHEME_SEMVER)
		return "semver";
	return NULL;
}
