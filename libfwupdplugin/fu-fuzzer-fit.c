#include "config.h"

#include "fu-fit-firmware.h"
#include "fu-fuzzer.h"
#include "fu-memory-input-stream.h"

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
	g_type_ensure(FU_TYPE_FIT_FIRMWARE);
	(void)g_setenv("G_DEBUG", "fatal-criticals", TRUE);
	(void)g_setenv("FWUPD_FUZZER_RUNNING", "1", TRUE);
	return 0;
}

int
LLVMFuzzerTestOneInput(const guint8 *data, gsize size)
{
	g_autoptr(FuFirmware) firmware = fu_fit_firmware_new();
	g_autoptr(FuFirmware) firmware_strict = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	GByteArray buf = {
		.data = (guint8 *)data,
		.len = size,
	};

	(void)fu_fuzzer_test_input(FU_FUZZER(firmware), &buf, NULL);
	if (size > 1024 * 1024)
		return 0;

	firmware_strict = fu_fit_firmware_new();
	stream = fu_memory_input_stream_new_from_data(data, size, NULL);
	if (!fu_firmware_parse_stream(firmware_strict,
				      stream,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH |
					  FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB |
					  FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID,
				      NULL))
		return 0;
	return 0;
}