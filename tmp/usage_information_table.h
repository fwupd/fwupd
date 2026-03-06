#ifndef usage_information_table_h
#define usage_information_table_h
#include <stdint.h>
#include <stdio.h>

#include "flash_id_usage_Information.h"

static const char *Path_Of_Usage_Information_Table = "FW/ldc_u4_usage_information_table.bin";

static const uint8_t UsageTableFlashID = 0xff;
static const int UsageTableSize = 4096;

typedef enum { Unsigned, RSA2048, RSA3072, ECC256, ECC384 } DSAType;

struct UsageInformation {
	uint8_t Totalnumber;
	uint8_t MajorVersion;
	uint8_t MinorVersion;
	DSAType Dsa;
	uint8_t IoTUpdateFlag;
	uint8_t CompositeFwVersion[4];
	uint8_t DockPid[2];
	uint8_t Crc32[4];
	struct FlashIdUsageInformation *FlashIdList;
};

#endif
