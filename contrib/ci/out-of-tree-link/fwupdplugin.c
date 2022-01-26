/* SPDX-License-Identifier: LGPL-2.1+ */

#undef NDEBUG
#include <fwupdplugin.h>

#include <assert.h>

int
main(void)
{
	assert(fu_common_vercmp_full("1.0", "2.1", FWUPD_VERSION_FORMAT_NUMBER) < 0);
	return 0;
}
