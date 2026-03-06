#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#include "dock_command_declaration.h"
#include "flash_id_usage_Information.h"
#include "usage_information_table.h"

int
BytesToInt(uint8_t *data, int length);
uint8_t *
IntToBytes(int size);
uint8_t *
ToBytes(uint32_t size);
uint32_t
Compute(uint8_t *buffer, size_t bufferlength, int offset, int length);
uint8_t *
GetBytes(struct UsageInformation targetUsageInformationTable);
int
Function1(uint8_t CmdClass,
	  uint8_t CmdId,
	  uint8_t FlashId,
	  uint8_t *data,
	  int dataSize,
	  uint8_t *output);
int
Function2(uint8_t CmdClass,
	  uint8_t CmdId,
	  uint8_t FlashId,
	  uint8_t *data,
	  int dataSize,
	  uint8_t *output);
uint8_t *
GetCommandBody1(uint8_t *data);
uint8_t *
GetCommandBody2(uint8_t *data);
bool
arraysEqual(uint8_t *array1, uint8_t *array2, size_t length);
int
ReadCompositeImageFile(char **data);

uint8_t *
GetCompositeData(int addr, int size, char *data)
{
	uint8_t *temp = (uint8_t *)calloc(size, sizeof(uint8_t));
	for (int i = 0; i < size; i++) {
		temp[i] = (uint8_t)data[addr + i];
	}
	return temp;
}

bool
CheckFwVerify(int flashId,
	      struct UsageInformation targetUsageInformationTable,
	      char *compositeImageData)
{
	int maxSize = BytesToInt(targetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	int targetFwSize =
	    BytesToInt(targetUsageInformationTable.FlashIdList[flashId].TargetFwFileSize, 4);
	if (targetFwSize + SignatureSize > maxSize)
		return false;

	int startAddr =
	    BytesToInt(targetUsageInformationTable.FlashIdList[flashId].PhysicalAddress, 4) +
	    SignatureSize;
	uint8_t *flashIdImageData = GetCompositeData(startAddr, targetFwSize, compositeImageData);
	int a = BytesToInt(targetUsageInformationTable.FlashIdList[flashId].TargetFwFileCrc32, 4);
	uint32_t b = Compute(flashIdImageData, targetFwSize, 0, targetFwSize);
	free(flashIdImageData);
	if ((uint32_t)a != b)
		return false;

	return true;
}

struct FlashIdAttribute
GetFlashIdAttribute(uint8_t *data)
{
	struct FlashIdAttribute fa;
	fa.FLashId = data[0];
	fa.Purpose = (ExternalFlashIdPurpose)data[1];

	int count = 2;
	uint8_t storageBuf[StorageSizeByteLength];
	for (int i = 0; i < StorageSizeByteLength; i++)
		storageBuf[i] = data[count++];
	fa.StorageSize = BytesToInt(storageBuf, StorageSizeByteLength);

	uint8_t eraseSizeBuf[EraseSizeByteLength];
	for (int i = 0; i < EraseSizeByteLength; i++)
		eraseSizeBuf[i] = data[count++];
	fa.EraseSize = BytesToInt(eraseSizeBuf, EraseSizeByteLength);

	uint8_t programSizeBuf[ProgramSizeByteLength];
	for (int i = 0; i < ProgramSizeByteLength; i++)
		programSizeBuf[i] = data[count++];
	fa.ProgramSize = BytesToInt(programSizeBuf, ProgramSizeByteLength);

	return fa;
}

void
TriggerPhase2(bool noUnplug)
{
	uint8_t DfuCtrl[2] = {0};
	uint8_t output1[Interface1Length];
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	if (noUnplug) {
		DfuCtrl[0] = Locked;
		DfuCtrl[1] = nonUnplug;
		int r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, DfuCtrl, 2, output1);
	} else {
		DfuCtrl[0] = Locked;
		DfuCtrl[1] = unplug;
		int r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, DfuCtrl, 2, output1);
	}
}

int
WriteUsageInformationTable(uint8_t *usageInformationData)
{
	int errorHandle = 0;
	// Get Usage Information Attribute
	uint8_t output1[Interface1Length];
	uint8_t output2[Interface2Length];
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int usageInformationAttributeData =
	    Function1(External_Flash, Get_Flash_Attribute, UsageTableFlashID, 0, 0, output1);
	if (usageInformationAttributeData != 0)
		return usageInformationAttributeData;
	uint8_t *usageInformationAttributeBody = GetCommandBody1(output1);
	struct FlashIdAttribute usageInformationAttribute =
	    GetFlashIdAttribute(usageInformationAttributeBody);
	free(usageInformationAttributeBody);

	// Set Usage Information Memory Access (Erase)
	for (int readBytes = 0; readBytes < UsageTableSize;
	     readBytes += usageInformationAttribute.EraseSize) {
		int address = UsageInfoStart + readBytes;
		uint8_t *eraseUsageInformationAddress = IntToBytes(address);
		uint8_t setUsageInformationMemoryErase[2 + AddressByteLength + EraseSizeByteLength];
		for (int i = 0; i < 2 + AddressByteLength + EraseSizeByteLength; i++)
			setUsageInformationMemoryErase[i] = 0;

		setUsageInformationMemoryErase[0] = Dock_Erase_With_Address;
		setUsageInformationMemoryErase[1] = 0x00;
		for (int i = 0; i < AddressByteLength; i++)
			setUsageInformationMemoryErase[2 + EraseSizeByteLength + i] =
			    eraseUsageInformationAddress[i];
		for (int i = 0; i < EraseSizeByteLength; i++)
			setUsageInformationMemoryErase[2 + i] =
			    IntToBytes(usageInformationAttribute.EraseSize)[i];
		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					UsageTableFlashID,
					setUsageInformationMemoryErase,
					2 + AddressByteLength + EraseSizeByteLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Set Usage Information Memory Access (Program)
	for (int readBytes = 0; readBytes < UsageTableSize;
	     readBytes += usageInformationAttribute.ProgramSize) {
		int address = UsageInfoStart + readBytes;
		uint8_t *addrBytes = IntToBytes(address);

		int payloadLength = 2 + AddressByteLength + ProgramSizeByteLength;
		uint8_t write[usageInformationAttribute.ProgramSize + payloadLength];
		for (int i = 0; i < usageInformationAttribute.ProgramSize + payloadLength; i++)
			write[i] = 0;
		write[0] = Dock_Program_With_Address;
		write[1] = 0x00;
		for (int i = 0; i < AddressByteLength; i++)
			write[2 + ProgramSizeByteLength + i] = addrBytes[i];
		for (int i = 0; i < ProgramSizeByteLength; i++)
			write[2 + i] = IntToBytes(usageInformationAttribute.ProgramSize)[i];
		for (int i = 0; i < usageInformationAttribute.ProgramSize; i++)
			write[payloadLength + i] = usageInformationData[readBytes + i];

		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					UsageTableFlashID,
					write,
					(usageInformationAttribute.ProgramSize + payloadLength),
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Get Usage Information Page Self-Verify
	uint8_t writeSelfVerify[1];
	writeSelfVerify[0] = CRC;
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int pageSelfVerifyData = Function1(External_Flash,
					   Get_Flash_Memory_Self_Verify,
					   UsageTableFlashID,
					   writeSelfVerify,
					   1,
					   output1);
	if (pageSelfVerifyData != 0)
		return pageSelfVerifyData;
	uint8_t *pageSelfVerifyBody = GetCommandBody1(output1);
	uint8_t pageSelfVerifyCrc[4];
	for (int i = 0; i < 4; i++) {
		pageSelfVerifyCrc[i] = pageSelfVerifyBody[1 + i];
		// printf("%02X\n",pageSelfVerifyCrc[i]);
	}

	// Usge Information CRC
	uint8_t uiCrc[4];
	for (int i = 0; i < 4; i++) {
		uiCrc[i] = usageInformationData[4092 + i];
		// printf("%02X\n",uiCrc[i]);
	}
	if (!arraysEqual(uiCrc, pageSelfVerifyCrc, 4))
		return USAGE_INFORMATION_PAGE_FAILED;

	return 0;
}

int
WriteFlashIdData(int flashId,
		 struct FlashIdAttribute flashIdAttribute,
		 struct UsageInformation changeTagetUsageInformationTable,
		 char *compositeImageData)
{
	int errorHandle = 0;
	uint8_t output1[Interface1Length];
	uint8_t output2[Interface2Length];
	// Check FW Data In Host Self-Verify
	if (!CheckFwVerify(flashId, changeTagetUsageInformationTable, compositeImageData))
		return UPDATE_USAGE_INFORMATION_PAGE_FAILED;

	// Set Flash Memory Access (Erase)
	uint8_t *eraseStartAddrBytes =
	    changeTagetUsageInformationTable.FlashIdList[flashId].PhysicalAddress;
	int targetMaxSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	int targetFwSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].TargetFwFileSize, 4);
	for (int eraseBytes = 0; eraseBytes < targetMaxSize;
	     eraseBytes += flashIdAttribute.EraseSize) {
		uint8_t setFlashMemoryErase[2 + EraseSizeByteLength + AddressByteLength];
		for (int i = 0; i < 2 + EraseSizeByteLength + AddressByteLength; i++)
			setFlashMemoryErase[i] = 0;
		setFlashMemoryErase[0] = Dock_Erase_With_Address;
		setFlashMemoryErase[1] = 0x00;
		int eraseAddr = BytesToInt(eraseStartAddrBytes, 4) + eraseBytes;
		for (int i = 0; i < EraseSizeByteLength; i++)
			setFlashMemoryErase[2 + i] = IntToBytes(flashIdAttribute.EraseSize)[i];
		for (int i = 0; i < AddressByteLength; i++)
			setFlashMemoryErase[2 + EraseSizeByteLength + i] = IntToBytes(eraseAddr)[i];

		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					flashId,
					setFlashMemoryErase,
					2 + EraseSizeByteLength + AddressByteLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Set Flash Memory Access (program)
	int flashIdStartAddr =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].PhysicalAddress, 4);
	int flashIdDataSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	uint8_t *flashIdCompositeData =
	    GetCompositeData(flashIdStartAddr, flashIdDataSize, compositeImageData);
	for (int readBytes = 0; readBytes < targetFwSize + SignatureSize;
	     readBytes += flashIdAttribute.ProgramSize) {
		int address = flashIdStartAddr + readBytes;
		uint8_t *addrBytes = IntToBytes(address);

		int payloadLength = 2 + AddressByteLength + ProgramSizeByteLength;
		uint8_t write[flashIdAttribute.ProgramSize + payloadLength];
		write[0] = Dock_Program_With_Address;
		write[1] = 0x00;
		for (int i = 0; i < AddressByteLength; i++)
			write[2 + ProgramSizeByteLength + i] = addrBytes[i];
		for (int i = 0; i < ProgramSizeByteLength; i++)
			write[2 + i] = IntToBytes(flashIdAttribute.ProgramSize)[i];
		for (int i = 0; i < flashIdAttribute.ProgramSize; i++)
			write[payloadLength + i] = flashIdCompositeData[readBytes + i];

		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					flashId,
					write,
					flashIdAttribute.ProgramSize + payloadLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	free(flashIdCompositeData);

	// Get Flash Usage Information Memory Self-Verify
	uint8_t writeFlashUpdateCheckSignature[1];
	writeFlashUpdateCheckSignature[0] = Signature;
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int flashUpdateCheckSignatureData = Function1(External_Flash,
						      Get_Flash_Memory_Self_Verify,
						      flashId,
						      writeFlashUpdateCheckSignature,
						      1,
						      output1);
	if (flashUpdateCheckSignatureData != 0)
		return flashUpdateCheckSignatureData;
	uint8_t *flashUpdateCheckSignatureBody = GetCommandBody1(output1);
	if (flashUpdateCheckSignatureBody[1] != (uint8_t)Pass)
		return UPDATE_DOCK_FAILED;

	uint8_t writeFlashUpdateCheckVerify[1];
	writeFlashUpdateCheckVerify[0] = CRC;
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int flashUpdateCheckVerifyData = Function1(External_Flash,
						   Get_Flash_Memory_Self_Verify,
						   flashId,
						   writeFlashUpdateCheckVerify,
						   1,
						   output1);
	if (flashUpdateCheckVerifyData != 0)
		return flashUpdateCheckVerifyData;
	uint8_t *flashUpdateCheckVerifyBody = GetCommandBody1(output1);
	uint8_t flashUpdateCheckVerifyCrc[4];
	for (int i = 0; i < 4; i++) {
		flashUpdateCheckVerifyCrc[i] = flashUpdateCheckVerifyBody[1 + i];
		// printf("%02X\n",flashUpdateCheckVerifyCrc[i]);
	}
	if (!arraysEqual(changeTagetUsageInformationTable.FlashIdList[flashId].TargetFwFileCrc32,
			 flashUpdateCheckVerifyCrc,
			 4))
		return UPDATE_DOCK_FAILED;

	return 0;
}

/**
 * @brief Execute firmware update procedure for the dock device.
 *
 * This function performs the full firmware update flow including:
 * - Load composite firmware image and Usage Information Table from file
 * - Request flash memory access from the device
 * - Parse and verify Usage Information Table
 * - Retrieve Flash ID list from the device
 * - Perform firmware programming for each component
 *
 * @param forceUpdate
 *        If true, firmware will be updated even if the current firmware version
 * is => the target version.
 *
 * @param noUnplug
 *        If true, skip unplug detection during the firmware update process.
 *
 * @return SUCCESS on success, otherwise returns an error code defined in the
 * firmware update error definitions.(int)
 */

int
FWUpdate(bool forceUpdate, bool noUnplug)
{
	// bool forceUpdate = false;
	// bool noUnplug = false;
	char *compositeImageData;
	int r;
	r = ReadCompositeImageFile(&compositeImageData);
	if (r != 0) {
		// printf("Read composite image error: %d\n",r);
		return r;
	}
	// Set Flash Memory Access (Request)
	uint8_t setFlashMemoryRequest[2];
	uint8_t output1[Interface1Length];
	uint8_t output2[Interface2Length];
	setFlashMemoryRequest[0] = AcccessCtrl;
	setFlashMemoryRequest[1] = Request;
	for (int i = 0; i < Interface2Length; i++)
		output2[i] = 0;
	r = Function2(External_Flash,
		      Set_Flash_Memory_Access,
		      0,
		      setFlashMemoryRequest,
		      2,
		      output2);
	if (r != 0) {
		printf("Set Flash Memory Access (Request) error: %d\n", r);
		return FWU_OTA_DEPLOYEEING;
	}
	// Read Target Usage Informaton Table
	struct stat fileInfo;
	struct UsageInformation targetUsageInformationTable;
	uint8_t ds[FlashIdUsageLength];
	FILE *fp;
	if (stat(Path_Of_Usage_Information_Table, &fileInfo) == 0) {
		if (fileInfo.st_size != UsageTableSize) {
			return USAGE_INFORMATION_FILE_ERROR;
		} else {
			/*read the file actions*/
			fp = fopen(Path_Of_Usage_Information_Table, "rb");
			char buffer[UsageTableSize];
			size_t bytesRead = fread(&buffer, sizeof(char), UsageTableSize, fp);
			targetUsageInformationTable.Totalnumber = (uint8_t)buffer[0];
			targetUsageInformationTable.MajorVersion =
			    (((uint8_t)buffer[1] >> 4) & 0x0f);
			targetUsageInformationTable.MinorVersion = ((uint8_t)buffer[1] & 0x0f);
			targetUsageInformationTable.Dsa = (DSAType)buffer[2];
			targetUsageInformationTable.IoTUpdateFlag = (uint8_t)buffer[3];
			for (int i = 0; i < 4; i++) {
				targetUsageInformationTable.CompositeFwVersion[i] =
				    (uint8_t)buffer[4 + i];
				targetUsageInformationTable.Crc32[i] =
				    (uint8_t)buffer[UsageTableSize - 4 + i];
			}
			for (int i = 0; i < 2; i++) {
				targetUsageInformationTable.DockPid[i] = (uint8_t)buffer[8 + i];
			}
			targetUsageInformationTable.FlashIdList =
			    (struct FlashIdUsageInformation *)calloc(
				targetUsageInformationTable.Totalnumber + 1,
				sizeof(struct FlashIdUsageInformation));
			for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
				for (int j = 0; j < FlashIdUsageLength; j++) {
					ds[j] = (uint8_t)buffer[i * 32 + j];
				}
				for (int j = 0; j < 4; j++) {
					targetUsageInformationTable.FlashIdList[i]
					    .PhysicalAddress[j] = ds[j];
					targetUsageInformationTable.FlashIdList[i].MaxSize[j] =
					    ds[4 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .CurrentFwVersion[j] = ds[8 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwVersion[j] = ds[12 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwFileSize[j] = ds[16 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwFileCrc32[j] = ds[20 + j];
					targetUsageInformationTable.FlashIdList[i].ComponentID =
					    ds[24];

					//
					targetUsageInformationTable.FlashIdList[i].Flag = false;
				}
			}
			if (BytesToInt(targetUsageInformationTable.Crc32, 4) !=
			    Compute(buffer,
				    sizeof(buffer) / sizeof(buffer[0]),
				    0,
				    UsageTableSize - 4))
				return USAGE_INFORMATION_CRC_FAILED;

			uint16_t targetPid =
			    (uint16_t)((targetUsageInformationTable.DockPid[0] << 8) +
				       targetUsageInformationTable.DockPid[1]);
			if (DockPid != targetPid)
				return XML_FILE_FORMAT_ERROR;
		}
	}
	// Check Flash Id List Count
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int getFlashIdListData = Function1(External_Flash, Get_Flash_ID_List, 0, 0, 0, output1);
	uint8_t flashIdListTotal = output1[6];
	uint8_t flashIdList[flashIdListTotal - 1];
	for (int i = 0; i < flashIdListTotal - 1; i++) {
		flashIdList[i] = output1[7 + i];
	}
	if ((targetUsageInformationTable.Totalnumber + 1) != flashIdListTotal)
		return USAGE_INFORMATION_CRC_FAILED;

	// Read MCU Usage Information Memory Access (Read)
	const int readCountByCycle = 256;
	int mcuUsageInformationCount = 0;
	uint8_t mcuUsageInformationData[UsageInfo];
	do {
		int payload = 8;
		uint8_t setmcuUsageInformationTable[payload];
		setmcuUsageInformationTable[0] = Dock_Read_With_Address;
		setmcuUsageInformationTable[1] = 0x00;

		uint8_t *size = IntToBytes(readCountByCycle);
		setmcuUsageInformationTable[2] = size[0];
		setmcuUsageInformationTable[3] = size[1];

		uint8_t *addr = IntToBytes(UsageInfoStart + mcuUsageInformationCount);
		setmcuUsageInformationTable[4] = addr[0];
		setmcuUsageInformationTable[5] = addr[1];
		setmcuUsageInformationTable[6] = addr[2];
		setmcuUsageInformationTable[7] = addr[3];

		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		int tempBytes = Function2(External_Flash,
					  Get_Flash_Memory_Access,
					  UsageTableFlashID,
					  setmcuUsageInformationTable,
					  payload,
					  output2);
		for (int i = 0; i < readCountByCycle; i++) {
			mcuUsageInformationData[mcuUsageInformationCount + i] =
			    output2[7 + payload + i];
		}
		mcuUsageInformationCount += readCountByCycle;

	} while (mcuUsageInformationCount < UsageInfo);

	// Check & Transfer MCU Usage Information Table
	uint8_t mcuUIcrc[4];
	for (int i = 0; i < 4; i++) {
		mcuUIcrc[i] = mcuUsageInformationData[UsageTableSize - 4 + i];
		// printf("%02X\n",mcuUIcrc[i]);
	}
	uint32_t t = Compute(mcuUsageInformationData, UsageTableSize, 0, 4092);
	uint8_t *computecrc = ToBytes(t);
	if (!arraysEqual(mcuUIcrc, computecrc, 4)) {
		forceUpdate = true;
	}

	// Check Package Version to Clean Update List
	bool bcdVerUpdateReq = false;
	if (forceUpdate)
		bcdVerUpdateReq = true;

	if (!forceUpdate) {
		for (int i = 0; i < Interface1Length; i++)
			output1[i] = 0;
		int GetBcdData =
		    Function1(Device_Information, Get_Firmware_Version, 0, 0, 0, output1);
		uint8_t *dockFWPackageVer = GetCommandBody1(output1);
		uint8_t *targetFWPackageVer = targetUsageInformationTable.CompositeFwVersion;
		int dockFWPackageVerInt =
		    (dockFWPackageVer[0] << 16) + (dockFWPackageVer[1] << 8) + dockFWPackageVer[2];
		int targetFWPackageVerInt = (targetFWPackageVer[1] << 16) +
					    (targetFWPackageVer[2] << 8) + targetFWPackageVer[3];
		free(dockFWPackageVer);

		if (targetFWPackageVerInt > dockFWPackageVerInt)
			bcdVerUpdateReq = true;
	}
	// Check Dock Component Need to Update
	struct FlashIdUsageInformation *FlashIdUpdateList;
	struct UsageInformation changeTagetUsageInformationTable = targetUsageInformationTable;

	// if (bcdVerUpdateReq) {
	if (forceUpdate) {
		for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
			changeTagetUsageInformationTable.FlashIdList[i] =
			    targetUsageInformationTable.FlashIdList[i];
			changeTagetUsageInformationTable.FlashIdList[i].Flag = true;
		}
	}

	// Compare The Component FW Version
	else {
		if (bcdVerUpdateReq) {
			struct UsageInformation mcuUsageInformationTable;
			struct FlashIdUsageInformation *mcuFlashIdList;
			mcuUsageInformationTable.Totalnumber = mcuUsageInformationData[0];
			mcuUsageInformationTable.MajorVersion =
			    ((mcuUsageInformationData[1] >> 4) & 0x0f);
			mcuUsageInformationTable.MinorVersion = (mcuUsageInformationData[1] & 0x0f);
			mcuUsageInformationTable.Dsa = (DSAType)mcuUsageInformationData[2];
			mcuUsageInformationTable.IoTUpdateFlag = mcuUsageInformationData[3];
			for (int i = 0; i < 4; i++) {
				mcuUsageInformationTable.CompositeFwVersion[i] =
				    mcuUsageInformationData[4 + i];
				mcuUsageInformationTable.Crc32[i] =
				    mcuUsageInformationData[UsageTableSize - 4 + i];
			}
			for (int i = 0; i < 2; i++) {
				mcuUsageInformationTable.DockPid[i] =
				    mcuUsageInformationData[8 + i];
			}
			mcuFlashIdList = (struct FlashIdUsageInformation *)calloc(
			    mcuUsageInformationTable.Totalnumber,
			    sizeof(struct FlashIdUsageInformation));
			for (int i = 1; i <= mcuUsageInformationTable.Totalnumber; i++) {
				for (int j = 0; j < FlashIdUsageLength; j++) {
					ds[j] = mcuUsageInformationData[i * 32 + j];
				}
				for (int j = 0; j < 4; j++) {
					mcuFlashIdList[i].PhysicalAddress[j] = ds[j];
					mcuFlashIdList[i].MaxSize[j] = ds[4 + j];
					mcuFlashIdList[i].CurrentFwVersion[j] = ds[8 + j];
					mcuFlashIdList[i].TargetFwVersion[j] = ds[12 + j];
					mcuFlashIdList[i].TargetFwFileSize[j] = ds[16 + j];
					mcuFlashIdList[i].TargetFwFileCrc32[j] = ds[20 + j];
					mcuFlashIdList[i].ComponentID = ds[24];
				}
			}

			for (int i = 1; i <= mcuUsageInformationTable.Totalnumber; i++) {
				if (!arraysEqual(
					mcuFlashIdList[i].TargetFwVersion,
					targetUsageInformationTable.FlashIdList[i].TargetFwVersion,
					4)) {
					changeTagetUsageInformationTable.FlashIdList[i] =
					    targetUsageInformationTable.FlashIdList[i];
					changeTagetUsageInformationTable.FlashIdList[i].Flag = true;
				} else {
					changeTagetUsageInformationTable.FlashIdList[i] =
					    mcuFlashIdList[i];
					changeTagetUsageInformationTable.FlashIdList[i].Flag =
					    false;
				}
			}
			free(mcuFlashIdList);
		}
	}

	// Check MCU in to phase-1 process
	bool NeedWriteTable = false;
	for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		if (changeTagetUsageInformationTable.FlashIdList[i].Flag) {
			NeedWriteTable = true;
			break;
		}
	}
	if (NeedWriteTable) {
		// Set Dock FW Update Ctrl
		uint8_t mcuUpdateCtrl[2];
		mcuUpdateCtrl[0] = nonLock;
		mcuUpdateCtrl[1] = InPhase1;
		for (int i = 0; i < Interface1Length; i++)
			output1[i] = 0;
		r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, mcuUpdateCtrl, 2, output1);

		// Clean Target Fw Version
		for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
			for (int j = 0; j < 4; j++)
				changeTagetUsageInformationTable.FlashIdList[i].TargetFwVersion[j] =
				    0x00;
		}

		// Write Usage Information Table
		uint8_t *changeTagetUsageInformationTableBytes =
		    GetBytes(changeTagetUsageInformationTable);
		r = WriteUsageInformationTable(changeTagetUsageInformationTableBytes);
		free(changeTagetUsageInformationTableBytes);
		if (r != 0)
			return r;
	}

	// Phase-1 Start
	for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		// Flash ID update start
		int flashId = i;

		// Get Flash ID Attribute
		for (int j = 0; j < Interface1Length; j++)
			output1[j] = 0;
		int flashIdAttributeData =
		    Function1(External_Flash, Get_Flash_Attribute, flashId, 0, 0, output1);
		if (flashIdAttributeData != 0)
			return flashIdAttributeData;
		uint8_t *flashIdAttributeBody = GetCommandBody1(output1);
		struct FlashIdAttribute flashIdAttribute =
		    GetFlashIdAttribute(flashIdAttributeBody);
		free(flashIdAttributeBody);
		// Check Component FW File
		if (flashIdAttribute.Purpose != FirmwareFile)
			continue;

		// Check Update Necessary
		if (!changeTagetUsageInformationTable.FlashIdList[flashId]
			 .Flag) // no need to update
			continue;

		// Write Flash ID Data
		r = WriteFlashIdData(flashId,
				     flashIdAttribute,
				     changeTagetUsageInformationTable,
				     compositeImageData);
		if (r != 0)
			return r;

		// Write Target FW Version
		changeTagetUsageInformationTable.FlashIdList[flashId] =
		    targetUsageInformationTable.FlashIdList[flashId];

		// Write Usage Information Table
		r = WriteUsageInformationTable(GetBytes(changeTagetUsageInformationTable));
		if (r != 0)
			return r;
	}

	// Set Flash Memory Access (Release)
	uint8_t SetFlashMemoryAccessRelease[2];
	SetFlashMemoryAccessRelease[0] = AcccessCtrl;
	SetFlashMemoryAccessRelease[1] = Release;
	for (int i = 0; i < Interface2Length; i++)
		output2[i] = 0;
	r = Function2(External_Flash,
		      Set_Flash_Memory_Access,
		      0,
		      SetFlashMemoryAccessRelease,
		      2,
		      output2);

	// Set Dock FW Update Ctrl
	if (bcdVerUpdateReq) {
		TriggerPhase2(noUnplug);
	}

	return 0;
}

bool
CheckDockReadyForEnterPhase2Update()
{
	bool rs = false;

	uint8_t buffer[65];
	rs = Function1(Dock, Get_Dock_Firmware_Upgrade_Ctrl, 0, 0, 0, buffer);
	uint8_t *DockFirmwareCtrlBody = GetCommandBody1(buffer);
	if (DockFirmwareCtrlBody[0] == Locked && DockFirmwareCtrlBody[1] == 2)
		rs = true;

	return rs;
}
