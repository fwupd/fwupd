#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_HID_FIRMWARE (fu_lenovo_hid_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoHidFirmware,
		     fu_lenovo_hid_firmware,
		     FU,
		     LENOVO_HID_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_lenovo_hid_firmware_new(void);
