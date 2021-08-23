/* SPDX-License-Identifier: LGPL-2.1+ */

#undef NDEBUG
#include <assert.h>
#include <fwupd.h>

int
main(void)
{
	assert(fwupd_error_to_string(FWUPD_ERROR_NOTHING_TO_DO) != NULL);
	return 0;
}
