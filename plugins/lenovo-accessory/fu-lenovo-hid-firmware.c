#include "config.h"

#include "fu-lenovo-hid-firmware.h"

struct _FuLenovoHidFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuLenovoHidFirmware, fu_lenovo_hid_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_lenovo_hid_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	return TRUE;
}

static void
fu_lenovo_hid_firmware_init(FuLenovoHidFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_lenovo_hid_firmware_class_init(FuLenovoHidFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_lenovo_hid_firmware_parse;
}
