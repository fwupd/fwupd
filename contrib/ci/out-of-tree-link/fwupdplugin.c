#undef NDEBUG
#include <assert.h>

#include <fwupdplugin.h>

int main (void)
{
    assert (fu_common_vercmp_full ("1.0", "2.1", FWUPD_VERSION_FORMAT_NUMBER) < 0);
    return 0;
}
