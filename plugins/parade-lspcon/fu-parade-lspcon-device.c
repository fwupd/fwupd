/*
 * Copyright 2021 Peter Marheine <pmarheine@chromium.org>
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-dpaux-struct.h"
#include "fu-parade-lspcon-device.h"
#include "fu-parade-lspcon-struct.h"

#define SPI_CTL_NOREAD	0x04 /* set to do a write-only transaction */
#define SPI_CTL_TRIGGER 0x01 /* set to begin executing command */

#define SPI_STATUS_BP_MASK 0x03 /* byte programming */
#define SPI_STATUS_SE_MASK 0x0C /* sector erase */
#define SPI_STATUS_CE_MASK 0x30 /* chip erase */

#define WR_PROTECT_DISABLE 0x10

#define FU_PARADE_LSPCON_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

/*
 * user1: 0x10000 - 0x20000
 * user2: 0x20000 - 0x30000
 * flag:  0x00002 - 0x00004
 */
struct _FuParadeLspconDevice {
	FuI2cDevice parent_instance;
	FuParadeLspconDeviceKind kind;
	guint8 active_partition;
	guint8 page_offset;
};

G_DEFINE_TYPE(FuParadeLspconDevice, fu_parade_lspcon_device, FU_TYPE_I2C_DEVICE)

static void
fu_parade_lspcon_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);
	fwupd_codec_string_append(str,
				  idt,
				  "Kind",
				  fu_parade_lspcon_device_kind_to_string(self->kind));
	fwupd_codec_string_append_hex(str, idt, "ActivePartition", self->active_partition);
	fwupd_codec_string_append_hex(str, idt, "PageOffset", self->page_offset);
}

static gboolean
fu_parade_lspcon_device_open(FuDevice *device, GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);

	if (!FU_DEVICE_CLASS(fu_parade_lspcon_device_parent_class)->open(device, error))
		return FALSE;

	/* general assumption is that page 2 is selected: code that uses another address
	 * should use an address guard to ensure it gets reset */
	return fu_i2c_device_set_address(FU_I2C_DEVICE(self),
					 self->page_offset + FU_PARADE_LSPCON_I2C_ADDR_PAGE2,
					 FALSE,
					 error);
}

/**
 * creates a scope in which the device's target I2C address is something
 * other than page 2, and resets it to page 2 when the scope is left.
 */
typedef struct {
	FuParadeLspconDevice *device;
} FuParadeLspconI2cAddressGuard;

static FuParadeLspconI2cAddressGuard *
fu_parade_lspcon_device_i2c_address_guard_new(FuParadeLspconDevice *self,
					      FuParadeLspconI2cAddr new_address,
					      GError **error)
{
	FuParadeLspconI2cAddressGuard *out;

	if (!fu_i2c_device_set_address(FU_I2C_DEVICE(self),
				       self->page_offset + new_address,
				       FALSE,
				       error))
		return NULL;
	out = g_new0(FuParadeLspconI2cAddressGuard, 1);
	out->device = self;
	return out;
}

static void
fu_parade_lspcon_device_i2c_address_guard_free(FuParadeLspconI2cAddressGuard *guard)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(guard->device);
	g_autoptr(GError) error_local = NULL;
	if (!fu_i2c_device_set_address(FU_I2C_DEVICE(self),
				       self->page_offset + FU_PARADE_LSPCON_I2C_ADDR_PAGE2,
				       FALSE,
				       &error_local)) {
		g_warning("failed to set page2 back: %s", error_local->message);
	}
	g_free(guard);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuParadeLspconI2cAddressGuard,
			      fu_parade_lspcon_device_i2c_address_guard_free);

static gboolean
fu_parade_lspcon_device_write_register(FuParadeLspconDevice *self,
				       guint8 register_addr,
				       guint8 value,
				       GError **error)
{
	guint8 transaction[] = {register_addr, value};
	return fu_i2c_device_write(FU_I2C_DEVICE(self), transaction, sizeof(transaction), error);
}

static gboolean
fu_parade_lspcon_device_read_register(FuParadeLspconDevice *self,
				      guint8 register_addr,
				      guint8 *value,
				      GError **error)
{
	FuI2cDevice *i2c_device = FU_I2C_DEVICE(self);
	if (!fu_i2c_device_write(i2c_device, &register_addr, 0x1, error))
		return FALSE;
	return fu_i2c_device_read(i2c_device, value, 0x1, error);
}

/* map the page containing the given address into page 7 */
static gboolean
fu_parade_lspcon_device_map_page(FuParadeLspconDevice *self, guint32 address, GError **error)
{
	if (!fu_parade_lspcon_device_write_register(self,
						    FU_PARADE_LSPCON_PAGE2_ADDR_FLASH_ADDR_HI,
						    address >> 16,
						    error))
		return FALSE;
	return fu_parade_lspcon_device_write_register(self,
						      FU_PARADE_LSPCON_PAGE2_ADDR_FLASH_ADDR_LO,
						      address >> 8,
						      error);
}

/* wait until the specified register masked with mask reads the expected
 * value, up to 10 seconds */
static gboolean
fu_parade_lspcon_device_poll_register(FuParadeLspconDevice *self,
				      guint8 register_address,
				      guint8 mask,
				      guint8 expected,
				      GError **error)
{
	guint8 value;
	g_autoptr(GTimer) timer = g_timer_new();

	do {
		if (!fu_parade_lspcon_device_read_register(self, register_address, &value, error))
			return FALSE;
		if ((value & mask) == expected)
			return TRUE;
	} while (g_timer_elapsed(timer, NULL) <= 10.0);

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_TIMED_OUT,
		    "register %x did not read %x (mask %x) within 10 seconds: read %x",
		    register_address,
		    expected,
		    mask,
		    value);
	return FALSE;
}

static gboolean
fu_parade_lspcon_device_flash_read(FuParadeLspconDevice *self,
				   guint32 base_address,
				   guint8 *data,
				   const gsize len,
				   FuProgress *progress,
				   GError **error)
{
	FuI2cDevice *i2c_device = FU_I2C_DEVICE(self);
	gsize offset = 0;

	while (offset < len) {
		/* page 7 reads always start from the base of the mapped window- we'll
		 * read the whole page then pull out the parts we care about, using the
		 * full page everywhere except possibly in the first and last reads */
		guint8 page_data[256] = {0x0};
		guint8 page_data_start = base_address & 0xFF;
		gsize page_data_take = MIN((gssize)len, 256 - page_data_start);
		g_autoptr(FuParadeLspconI2cAddressGuard) guard = NULL;

		if (!fu_parade_lspcon_device_map_page(self, base_address, error))
			return FALSE;
		guard =
		    fu_parade_lspcon_device_i2c_address_guard_new(self,
								  FU_PARADE_LSPCON_I2C_ADDR_PAGE7,
								  error);
		if (guard == NULL)
			return FALSE;
		if (!fu_i2c_device_read(i2c_device, page_data, 256, error))
			return FALSE;

		if (!fu_memcpy_safe(data,
				    len,
				    offset,
				    page_data,
				    sizeof(page_data),
				    page_data_start,
				    page_data_take,
				    error))
			return FALSE;
		base_address += page_data_take;
		offset += page_data_take;

		fu_progress_set_percentage_full(progress, offset, len);
	}

	return TRUE;
}

static gboolean
fu_parade_lspcon_device_flash_transmit_command(FuParadeLspconDevice *self,
					       const guint8 *command,
					       gsize command_len,
					       GError **error)
{
	/* write length field is 4 bits wide */
	g_return_val_if_fail(command_len > 0 && command_len <= 16, FALSE);

	/* fill transmit buffer */
	for (gsize i = 0; i < command_len; i++) {
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_WR_FIFO,
							    command[i],
							    error))
			return FALSE;
	}

	/* set command length */
	if (!fu_parade_lspcon_device_write_register(self,
						    FU_PARADE_LSPCON_PAGE2_ADDR_SPI_LEN,
						    command_len - 1,
						    error))
		return FALSE;

	/* execute operation */
	return fu_parade_lspcon_device_write_register(self,
						      FU_PARADE_LSPCON_PAGE2_ADDR_SPI_CTL,
						      SPI_CTL_NOREAD | SPI_CTL_TRIGGER,
						      error);
}

/*
 * set the flash Write Enable Latch, permitting the next program, erase or
 * status register write operation.
 */
static gboolean
fu_parade_lspcon_device_flash_enable_write(FuParadeLspconDevice *self, GError **error)
{
	const guint8 write_enable[] = {0x06};
	return fu_parade_lspcon_device_flash_transmit_command(self,
							      write_enable,
							      sizeof(write_enable),
							      error);
}

static gboolean
fu_parade_lspcon_device_flash_read_status(FuParadeLspconDevice *self, guint8 *value, GError **error)
{
	if (!fu_parade_lspcon_device_write_register(self,
						    FU_PARADE_LSPCON_PAGE2_ADDR_WR_FIFO,
						    0x05,
						    error))
		return FALSE;
	if (!fu_parade_lspcon_device_write_register(self,
						    FU_PARADE_LSPCON_PAGE2_ADDR_SPI_LEN,
						    0,
						    error))
		return FALSE;
	if (!fu_parade_lspcon_device_write_register(self,
						    FU_PARADE_LSPCON_PAGE2_ADDR_SPI_CTL,
						    SPI_CTL_TRIGGER,
						    error))
		return FALSE;

	/* wait for command completion */
	if (!fu_parade_lspcon_device_poll_register(self,
						   FU_PARADE_LSPCON_PAGE2_ADDR_SPI_CTL,
						   SPI_CTL_TRIGGER,
						   0,
						   error))
		return FALSE;

	/* read SR value */
	return fu_parade_lspcon_device_read_register(self,
						     FU_PARADE_LSPCON_PAGE2_ADDR_RD_FIFO,
						     value,
						     error);
}

/* poll the flash status register for operation completion */
static gboolean
fu_parade_lspcon_device_flash_wait_ready(FuParadeLspconDevice *self, GError **error)
{
	g_autoptr(GTimer) timer = g_timer_new();

	do {
		guint8 status_register;
		if (!fu_parade_lspcon_device_flash_read_status(self, &status_register, error))
			return FALSE;

		/* BUSY bit clears on completion */
		if ((status_register & 1) == 0)
			return TRUE;

		/* flash operations generally take between 1ms and 4s; polling
		 * at 1000 Hz is still quite responsive and not overly slow */
		fu_device_sleep(FU_DEVICE(self), 1); /* ms */
	} while (g_timer_elapsed(timer, NULL) <= 10.0);

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "flash did not become ready within 10 seconds");
	return FALSE;
}

static gboolean
fu_parade_lspcon_device_prepare(FuDevice *device,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);
	const guint8 write_sr_volatile[] = {0x50};
	const guint8 write_sr_disable_bp[] = {
	    0x01, /* write SR */
	    0x80, /* write protect follows /WP signal, no block protection */
	    0x00};

	/* deassert flash /WP */
	if (!fu_parade_lspcon_device_write_register(self,
						    FU_PARADE_LSPCON_PAGE2_ADDR_WR_PROTECT,
						    WR_PROTECT_DISABLE,
						    error))
		return FALSE;

	/* disable flash protection until next power-off */
	if (!fu_parade_lspcon_device_flash_transmit_command(self,
							    write_sr_volatile,
							    sizeof(write_sr_volatile),
							    error))
		return FALSE;
	if (!fu_parade_lspcon_device_flash_transmit_command(self,
							    write_sr_disable_bp,
							    sizeof(write_sr_disable_bp),
							    error))
		return FALSE;

	/* wait for SR write to complete */
	return fu_parade_lspcon_device_flash_wait_ready(self, error);
}

static gboolean
fu_parade_lspcon_device_cleanup(FuDevice *device,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);
	const guint8 write_sr_volatile[] = {0x50};
	const guint8 write_sr_enable_bp[] = {0x01, 0x8c, 0x00};

	/* re-enable flash protection */
	if (!fu_parade_lspcon_device_flash_transmit_command(self,
							    write_sr_volatile,
							    sizeof(write_sr_volatile),
							    error))
		return FALSE;
	if (!fu_parade_lspcon_device_flash_transmit_command(self,
							    write_sr_enable_bp,
							    sizeof(write_sr_enable_bp),
							    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* reassert /WP to flash */
	return fu_parade_lspcon_device_write_register(self,
						      FU_PARADE_LSPCON_PAGE2_ADDR_WR_PROTECT,
						      0,
						      error);
}

static gboolean
fu_parade_lspcon_device_flash_write(FuParadeLspconDevice *self,
				    guint32 base_address,
				    GInputStream *stream,
				    FuProgress *progress,
				    GError **error)
{
	FuI2cDevice *i2c_device = FU_I2C_DEVICE(self);
	const guint8 unlock_writes[] = {0xaa, 0x55, 0x50, 0x41, 0x52, 0x44};
	g_autoptr(FuChunkArray) chunks = NULL;

	/* unlock map writes by writing the magic sequence */
	for (gsize i = 0; i < sizeof(unlock_writes); i++) {
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_MAP_WRITE,
							    unlock_writes[i],
							    error))
			return FALSE;
	}

	/* reset clt2SPI, required before write */
	if (self->kind == FU_PARADE_LSPCON_DEVICE_KIND_PS175) {
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_SPICFG3,
							    0x20,
							    error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), 100); /* ms */
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_SPICFG3,
							    0,
							    error))
			return FALSE;
	}

	chunks = fu_chunk_array_new_from_stream(stream, base_address, 256, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (gsize i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chunk = NULL;
		guint32 address;
		guint32 chunk_size;
		guint8 write_data[257] = {0x0};
		g_autoptr(FuParadeLspconI2cAddressGuard) guard = NULL;

		/* prepare chunk */
		chunk = fu_chunk_array_index(chunks, i, error);
		if (chunk == NULL)
			return FALSE;

		/* map target address range in page 7 */
		address = fu_chunk_get_address(chunk);
		if (!fu_parade_lspcon_device_map_page(self, address, error))
			return FALSE;

		/* write data to page 7 memory window */
		guard =
		    fu_parade_lspcon_device_i2c_address_guard_new(self,
								  FU_PARADE_LSPCON_I2C_ADDR_PAGE7,
								  error);
		if (guard == NULL)
			return FALSE;

		/* page write is prefixed with an offset:
		 * we always start from offset 0 */
		chunk_size = fu_chunk_get_data_sz(chunk);
		if (!fu_memcpy_safe(write_data,
				    sizeof(write_data),
				    1,
				    fu_chunk_get_data(chunk),
				    chunk_size,
				    0,
				    chunk_size,
				    error))
			return FALSE;

		if (!fu_i2c_device_write(i2c_device, write_data, chunk_size + 1, error))
			return FALSE;

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* re-lock map writes */
	return fu_parade_lspcon_device_write_register(self,
						      FU_PARADE_LSPCON_PAGE2_ADDR_MAP_WRITE,
						      0,
						      error);
}

static gboolean
fu_parade_lspcon_device_flash_erase_block(FuParadeLspconDevice *self,
					  guint32 base_address,
					  guint32 size,
					  GError **error)
{
	const guint8 block_erase[] = {0xd8, base_address >> 16, base_address >> 8, base_address};

	g_debug("flash erase block at %#x", base_address);
	if (!fu_parade_lspcon_device_flash_enable_write(self, error))
		return FALSE;

	/* GPIO7=IROMW, GPIO3=interrupt/gpio, SPIROM writable */
	if (self->kind == FU_PARADE_LSPCON_DEVICE_KIND_PS185) {
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_IOCFG1,
							    0x88,
							    error))
			return FALSE;
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_SPICFG3,
							    0x08,
							    error))
			return FALSE;
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_I_ROM_CTRL,
							    0x03,
							    error))
			return FALSE;
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_ROM_WP_CFG,
							    0x07,
							    error))
			return FALSE;
	}
	if (!fu_parade_lspcon_device_flash_transmit_command(self,
							    block_erase,
							    sizeof(block_erase),
							    error))
		return FALSE;

	/* wait for command completion */
	if (!fu_parade_lspcon_device_poll_register(self,
						   FU_PARADE_LSPCON_PAGE2_ADDR_SPI_STATUS,
						   SPI_STATUS_SE_MASK,
						   0,
						   error))
		return FALSE;

	/* wait for flash to complete erase */
	if (!fu_parade_lspcon_device_flash_wait_ready(self, error))
		return FALSE;

	/* GPIO7=GPIO, SPIROM non-writable */
	if (self->kind == FU_PARADE_LSPCON_DEVICE_KIND_PS185) {
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_IOCFG1,
							    0x00,
							    error))
			return FALSE;
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_SPICFG3,
							    0x00,
							    error))
			return FALSE;
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_I_ROM_CTRL,
							    0x06,
							    error))
			return FALSE;
		if (!fu_parade_lspcon_device_write_register(self,
							    FU_PARADE_LSPCON_PAGE2_ADDR_ROM_WP_CFG,
							    0x00,
							    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_lspcon_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);
	gsize blocksz = fu_device_get_firmware_size_max(device);
	/* if the boot partition is active we could flash either, but prefer
	 * the first */
	const guint8 target_partition = self->active_partition == 1 ? 2 : 1;
	const guint32 target_address = target_partition << 16;
	const guint8 flag_data[] = {0x55, 0xaa, target_partition, 1 - target_partition};
	g_autofree guint8 *readback_buf = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GInputStream) flag_data_stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 70, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 25, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 3, "device-write-boot");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, "device-verify-boot");

	/* erase entire target partition (one flash block) */
	if (!fu_parade_lspcon_device_flash_erase_block(self, target_address, blocksz, error)) {
		g_prefix_error(error, "failed to erase flash partition %d: ", target_partition);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_parade_lspcon_device_flash_write(self,
						 target_address,
						 stream,
						 fu_progress_get_child(progress),
						 error)) {
		g_prefix_error(error,
			       "failed to write firmware to partition %d: ",
			       target_partition);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* read back written image to verify */
	readback_buf = g_malloc0(blocksz);
	if (!fu_parade_lspcon_device_flash_read(self,
						target_address,
						readback_buf,
						blocksz,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	buf = fu_input_stream_read_byte_array(stream, target_address, blocksz, NULL, error);
	if (buf == NULL)
		return FALSE;
	if (!fu_memcmp_safe(buf->data, buf->len, 0x0, readback_buf, blocksz, 0x0, blocksz, error)) {
		g_prefix_error(error, "flash contents do not match: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* erase flag partition */
	if (!fu_parade_lspcon_device_flash_erase_block(self, 0, blocksz, error))
		return FALSE;

	/* write flag indicating device should boot the target partition */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	flag_data_stream =
	    G_INPUT_STREAM(g_memory_input_stream_new_from_data(flag_data, sizeof(flag_data), NULL));
	if (!fu_parade_lspcon_device_flash_write(self,
						 0,
						 flag_data_stream,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify flag partition */
	if (!fu_parade_lspcon_device_flash_read(self,
						0,
						readback_buf,
						sizeof(flag_data),
						fu_progress_get_child(progress),
						error))
		return FALSE;
	if (!fu_memcmp_safe(flag_data,
			    sizeof(flag_data),
			    0x0,
			    readback_buf,
			    blocksz,
			    0x0,
			    MIN(sizeof(flag_data), blocksz),
			    error)) {
		g_prefix_error(error, "flag partition contents do not match: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_lspcon_device_set_dev_id(FuParadeLspconDevice *self, const gchar *dev_id, GError **error)
{
	g_autofree gchar *model = g_strndup(dev_id, 3);
	g_autofree gchar *name = g_strdup_printf("PS%s", model);
	g_autofree gchar *kind = g_strdup_printf("ps%s", model);

	/* fallback name */
	if (fu_device_get_name(FU_DEVICE(self)) == NULL)
		fu_device_set_name(FU_DEVICE(self), name);

	/* detect kind */
	self->kind = fu_parade_lspcon_device_kind_from_string(kind);
	if (self->kind == FU_PARADE_LSPCON_DEVICE_KIND_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s not supported",
			    kind);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_parade_lspcon_device_set_ieee_oui(FuParadeLspconDevice *self, guint32 ieee_oui, GError **error)
{
	g_autofree gchar *vid = g_strdup_printf("%06X", ieee_oui);
	fu_device_build_vendor_id(FU_DEVICE(self), "OUI", vid);
	fu_device_add_instance_str(FU_DEVICE(self), "VID", vid);
	return fu_device_build_instance_id_full(FU_DEVICE(self),
						FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						error,
						"OUI",
						"VID",
						NULL);
}

static gboolean
fu_parade_lspcon_device_ensure_dpcd(FuParadeLspconDevice *self, GError **error)
{
	guint8 buf[FU_STRUCT_DPAUX_DPCD_SIZE] = {0x0};
	g_autofree gchar *dev_id = NULL;
	g_autoptr(FuParadeLspconI2cAddressGuard) guard = NULL;
	g_autoptr(FuStructDpauxDpcd) st = NULL;

	guard = fu_parade_lspcon_device_i2c_address_guard_new(self,
							      FU_PARADE_LSPCON_I2C_ADDR_PAGE1,
							      error);
	if (guard == NULL)
		return FALSE;
	for (guint i = 0; i < sizeof(buf); i++) {
		if (!fu_parade_lspcon_device_read_register(self,
							   FU_PARADE_LSPCON_PAGE1_ADDR_DPCD + i,
							   buf + i,
							   error))
			return FALSE;
	}
	st = fu_struct_dpaux_dpcd_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	dev_id = fu_struct_dpaux_dpcd_get_dev_id(st);
	if (dev_id != NULL) {
		if (!fu_parade_lspcon_device_set_dev_id(self, dev_id, error))
			return FALSE;
	}
	if (fu_struct_dpaux_dpcd_get_ieee_oui(st) != 0x0) {
		if (!fu_parade_lspcon_device_set_ieee_oui(self,
							  fu_struct_dpaux_dpcd_get_ieee_oui(st),
							  error))
			return FALSE;
	}
	fu_device_set_version_raw(FU_DEVICE(self), fu_struct_dpaux_dpcd_get_fw_ver(st));

	/* success */
	return TRUE;
}

static gboolean
fu_parade_lspcon_device_ensure_active_flash_partition_internal(FuParadeLspconDevice *self,
							       GError **error)
{
	guint8 data = 0x0;
	g_autoptr(FuParadeLspconI2cAddressGuard) guard = NULL;

	/* read currently-running flash partition number */
	guard = fu_parade_lspcon_device_i2c_address_guard_new(self,
							      FU_PARADE_LSPCON_I2C_ADDR_PAGE5,
							      error);
	if (guard == NULL)
		return FALSE;
	if (!fu_parade_lspcon_device_read_register(self,
						   FU_PARADE_LSPCON_PAGE5_ADDR_ACTIVE_PARTITION,
						   &data,
						   error))
		return FALSE;

	self->active_partition = data;
	return TRUE;
}

static gboolean
fu_parade_lspcon_device_ensure_active_flash_partition(FuParadeLspconDevice *self, GError **error)
{
	guint8 page_offsets[] = {0x0, 0x40};

	/* try to find the correct page offset */
	for (guint i = 0; i < G_N_ELEMENTS(page_offsets); i++) {
		g_autoptr(GError) error_local = NULL;
		self->page_offset = page_offsets[i];
		if (!fu_parade_lspcon_device_ensure_active_flash_partition_internal(self,
										    &error_local)) {
			g_debug("ignoring: %s", error_local->message);
		} else {
			g_debug("got flash partition with page offset 0x%x", self->page_offset);
			return TRUE;
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "page offset could not be auto-detected");
	return FALSE;
}

static gboolean
fu_parade_lspcon_device_setup(FuDevice *device, GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);
	FuContext *context = fu_device_get_context(device);

	/* try to also find the correct page offset */
	if (!fu_parade_lspcon_device_ensure_active_flash_partition(self, error))
		return FALSE;

	/* verify active partition */
	if (self->active_partition < 1 || self->active_partition > 3) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unexpected active flash partition: %d",
			    self->active_partition);
		return FALSE;
	}

	/* DPCD is mirrored into PAGE1 */
	if (!fu_parade_lspcon_device_ensure_dpcd(self, error))
		return FALSE;

	/* unique enough for a firmware match */
	fu_device_add_instance_str(device,
				   "FAMILY",
				   fu_context_get_hwid_value(context, FU_HWIDS_KEY_FAMILY));
	return fu_device_build_instance_id(device, error, "I2C", "NAME", "FAMILY", NULL);
}

static gboolean
fu_parade_lspcon_device_set_mpu_running(FuParadeLspconDevice *self,
					gboolean running,
					GError **error)
{
	/* reset */
	if (!fu_parade_lspcon_device_write_register(self,
						    FU_PARADE_LSPCON_PAGE2_ADDR_MPU,
						    0xc0,
						    error))
		return FALSE;

	/* release reset, set MPU active or not */
	return fu_parade_lspcon_device_write_register(self,
						      FU_PARADE_LSPCON_PAGE2_ADDR_MPU,
						      running ? 0 : 0x40,
						      error);
}

static gboolean
fu_parade_lspcon_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);
	return fu_parade_lspcon_device_set_mpu_running(self, FALSE, error);
}

static gboolean
fu_parade_lspcon_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);
	return fu_parade_lspcon_device_set_mpu_running(self, TRUE, error);
}

static GBytes *
fu_parade_lspcon_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuParadeLspconDevice *self = FU_PARADE_LSPCON_DEVICE(device);
	gsize blocksz = fu_device_get_firmware_size_max(device);
	g_autofree guint8 *buf = g_malloc0(blocksz);

	if (!fu_parade_lspcon_device_flash_read(self,
						self->active_partition * blocksz,
						buf,
						blocksz,
						progress,
						error))
		return NULL;
	return g_bytes_new_take(g_steal_pointer(&buf), blocksz);
}

static void
fu_parade_lspcon_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_parade_lspcon_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%u.%u",
			       (guint)(version_raw >> 16) & 0xFF,
			       (guint)(version_raw >> 8) & 0xFF);
}

static void
fu_parade_lspcon_device_init(FuParadeLspconDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.paradetech.ps176");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_set_firmware_size(FU_DEVICE(self), 0x10000);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_parade_lspcon_device_class_init(FuParadeLspconDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_parade_lspcon_device_to_string;
	device_class->setup = fu_parade_lspcon_device_setup;
	device_class->open = fu_parade_lspcon_device_open;
	device_class->reload = fu_parade_lspcon_device_setup;
	device_class->detach = fu_parade_lspcon_device_detach;
	device_class->prepare = fu_parade_lspcon_device_prepare;
	device_class->write_firmware = fu_parade_lspcon_device_write_firmware;
	device_class->cleanup = fu_parade_lspcon_device_cleanup;
	device_class->attach = fu_parade_lspcon_device_attach;
	device_class->dump_firmware = fu_parade_lspcon_device_dump_firmware;
	device_class->set_progress = fu_parade_lspcon_device_set_progress;
	device_class->convert_version = fu_parade_lspcon_device_convert_version;
}
