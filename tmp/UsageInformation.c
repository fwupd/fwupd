#include <stdlib.h>

#include "usage_information_table.h"
uint8_t *
ToBytes(uint32_t size);
uint32_t
Compute(uint8_t *buffer, size_t bufferlength, int offset, int length);

uint8_t *
GetBytes(struct UsageInformation targetUsageInformationTable)
{
	uint8_t *table = (uint8_t *)calloc(UsageTableSize, sizeof(uint8_t));
	// Header
	table[0] = targetUsageInformationTable.Totalnumber;
	table[1] = (targetUsageInformationTable.MajorVersion << 4) +
		   (targetUsageInformationTable.MinorVersion & 0x0f);
	table[2] = (uint8_t)targetUsageInformationTable.Dsa;
	table[3] = targetUsageInformationTable.IoTUpdateFlag;
	for (int i = 0; i < 4; i++)
		table[4 + i] = targetUsageInformationTable.CompositeFwVersion[i];
	for (int i = 0; i < 2; i++)
		table[8 + i] = targetUsageInformationTable.DockPid[i];
	// Body
	for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		int flashId = i;
		uint8_t flashIdData[FlashIdUsageLength];
		for (int j = 0; j < FlashIdUsageLength; j++)
			flashIdData[j] = 0;
		for (int j = 0; j < 4; j++) {
			flashIdData[0 + j] =
			    targetUsageInformationTable.FlashIdList[i].PhysicalAddress[j];
			flashIdData[4 + j] = targetUsageInformationTable.FlashIdList[i].MaxSize[j];
			flashIdData[8 + j] =
			    targetUsageInformationTable.FlashIdList[i].CurrentFwVersion[j];
			flashIdData[12 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwVersion[j];
			flashIdData[16 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwFileSize[j];
			flashIdData[20 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwFileCrc32[j];
			flashIdData[24] = targetUsageInformationTable.FlashIdList[i].ComponentID;
		}
		for (int j = 0; j < FlashIdUsageLength; j++) {
			int addr = (flashId * 32) + j;
			table[addr] = flashIdData[j];
			// printf("%02X ",table[addr]);
		}
	}
	// Crc32
	uint32_t tableCrc = Compute(table, UsageTableSize, 0, 4092);
	uint8_t *computecrc = ToBytes(tableCrc);
	for (int i = 0; i < 4; i++) {
		table[4092 + i] = computecrc[i];
		// printf("CRC: %02X\n",computecrc[i]);
	}

	return table;
}