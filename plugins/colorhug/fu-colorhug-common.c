/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-colorhug-common.h"

const gchar *
ch_strerror (ChError error_enum)
{
	if (error_enum == CH_ERROR_NONE)
		return "Success";
	if (error_enum == CH_ERROR_UNKNOWN_CMD)
		return "Unknown command";
	if (error_enum == CH_ERROR_WRONG_UNLOCK_CODE)
		return "Wrong unlock code";
	if (error_enum == CH_ERROR_NOT_IMPLEMENTED)
		return "Not implemented";
	if (error_enum == CH_ERROR_UNDERFLOW_SENSOR)
		return "Underflow of sensor";
	if (error_enum == CH_ERROR_NO_SERIAL)
		return "No serial";
	if (error_enum == CH_ERROR_WATCHDOG)
		return "Watchdog";
	if (error_enum == CH_ERROR_INVALID_ADDRESS)
		return "Invalid address";
	if (error_enum == CH_ERROR_INVALID_LENGTH)
		return "Invalid length";
	if (error_enum == CH_ERROR_INVALID_CHECKSUM)
		return "Invalid checksum";
	if (error_enum == CH_ERROR_INVALID_VALUE)
		return "Invalid value";
	if (error_enum == CH_ERROR_UNKNOWN_CMD_FOR_BOOTLOADER)
		return "Unknown command for bootloader";
	if (error_enum == CH_ERROR_OVERFLOW_MULTIPLY)
		return "Overflow of multiply";
	if (error_enum == CH_ERROR_OVERFLOW_ADDITION)
		return "Overflow of addition";
	if (error_enum == CH_ERROR_OVERFLOW_SENSOR)
		return "Overflow of sensor";
	if (error_enum == CH_ERROR_OVERFLOW_STACK)
		return "Overflow of stack";
	if (error_enum == CH_ERROR_NO_CALIBRATION)
		return "No calibration";
	if (error_enum == CH_ERROR_DEVICE_DEACTIVATED)
		return "Device deactivated";
	if (error_enum == CH_ERROR_INCOMPLETE_REQUEST)
		return "Incomplete previous request";
	if (error_enum == CH_ERROR_SELF_TEST_SENSOR)
		return "Self test failed: Sensor";
	if (error_enum == CH_ERROR_SELF_TEST_RED)
		return "Self test failed: Red";
	if (error_enum == CH_ERROR_SELF_TEST_GREEN)
		return "Self test failed: Green";
	if (error_enum == CH_ERROR_SELF_TEST_BLUE)
		return "Self test failed: Blue";
	if (error_enum == CH_ERROR_SELF_TEST_MULTIPLIER)
		return "Self test failed: Multiplier";
	if (error_enum == CH_ERROR_SELF_TEST_COLOR_SELECT)
		return "Self test failed: Color Select";
	if (error_enum == CH_ERROR_SELF_TEST_TEMPERATURE)
		return "Self test failed: Temperature";
	if (error_enum == CH_ERROR_INVALID_CALIBRATION)
		return "Invalid calibration";
	if (error_enum == CH_ERROR_SRAM_FAILED)
		return "SRAM failed";
	if (error_enum == CH_ERROR_OUT_OF_MEMORY)
		return "Out of memory";
	if (error_enum == CH_ERROR_SELF_TEST_I2C)
		return "Self test failed: I2C";
	if (error_enum == CH_ERROR_SELF_TEST_ADC_VDD)
		return "Self test failed: ADC Vdd";
	if (error_enum == CH_ERROR_SELF_TEST_ADC_VSS)
		return "Self test failed: ADC Vss";
	if (error_enum == CH_ERROR_SELF_TEST_ADC_VREF)
		return "Self test failed: ADC Vref";
	if (error_enum == CH_ERROR_I2C_SLAVE_ADDRESS)
		return "I2C set slave address failed";
	if (error_enum == CH_ERROR_I2C_SLAVE_CONFIG)
		return "I2C set slave config failed";
	if (error_enum == CH_ERROR_SELF_TEST_EEPROM)
		return "Self test failed: EEPROM";
	return NULL;
}
