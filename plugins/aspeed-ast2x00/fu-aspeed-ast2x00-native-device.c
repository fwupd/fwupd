/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/mman.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "fu-aspeed-ast2x00-native-device.h"

struct _FuAspeedAst2x00NativeDevice {
	FuAspeedAst2x00Device parent_instance;
	gpointer io;
	gboolean ilpc_readonly;
	gboolean ilpc_disabled;
	gboolean superio_disabled;
	gboolean debug_disabled;
	gboolean debug_uart_disabled;
};

G_DEFINE_TYPE(FuAspeedAst2x00NativeDevice,
	      fu_aspeed_ast2x00_native_device,
	      FU_TYPE_ASPEED_AST2X00_DEVICE)

#define AST_SOC_IO     0x1e600000
#define AST_SOC_IO_SCU 0x1e6e2000
#define AST_SOC_IO_LPC 0x1e789000
#define AST_SOC_IO_LEN 0x00200000

// FIXME move these to the spec
#define FWUPD_SECURITY_ATTR_ID_ASPEED_ILPC2AHB_READWRITE "org.fwupd.hsi.Aspeed.iLPC2AHB.ReadWrite"
#define FWUPD_SECURITY_ATTR_ID_ASPEED_ILPC2AHB_READONLY	 "org.fwupd.hsi.Aspeed.iLPC2AHB.ReadOnly"
#define FWUPD_SECURITY_ATTR_ID_ASPEED_UART_DEBUG	 "org.fwupd.hsi.Aspeed.iLPC2AHB.UartDebug"

static void
fu_aspeed_ast2x00_native_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAspeedAst2x00NativeDevice *self = FU_ASPEED_AST2X00_NATIVE_DEVICE(device);

	/* FuAspeedAst2x00Device->to_string */
	FU_DEVICE_CLASS(fu_aspeed_ast2x00_native_device_parent_class)->to_string(device, idt, str);

	fu_string_append_kx(str, idt, "IlpcReadonly", self->ilpc_readonly);
	fu_string_append_kx(str, idt, "IlpcDisabled", self->ilpc_disabled);
	fu_string_append_kx(str, idt, "SuperioDisabled", self->superio_disabled);
	fu_string_append_kx(str, idt, "DebugDisabled", self->debug_disabled);
	fu_string_append_kx(str, idt, "DebugUartDisabled", self->debug_uart_disabled);
}

static gboolean
fu_aspeed_ast2x00_native_device_setup_read_u32(FuAspeedAst2x00NativeDevice *self,
					       gsize phys_addr,
					       guint32 *value,
					       GError **error)
{
	return fu_memread_uint32_safe((guint8 *)self->io,
				      AST_SOC_IO_LEN,
				      phys_addr - AST_SOC_IO,
				      value,
				      G_LITTLE_ENDIAN,
				      error);
}

#define BIT_IS_SET(val, bit) (((val >> bit) & 0b1) > 0)

static gboolean
fu_aspeed_ast2x00_native_device_setup_xx1(FuAspeedAst2x00NativeDevice *self, GError **error)
{
	guint32 val;
	guint32 val2;
	FuAspeedAst2x00Revision rev =
	    fu_aspeed_ast2x00_device_get_revision(FU_ASPEED_AST2X00_DEVICE(self));

	if (rev == FU_ASPEED_AST2400 || rev == FU_ASPEED_AST2500) {
		if (!fu_aspeed_ast2x00_native_device_setup_read_u32(self,
								    AST_SOC_IO_SCU + 0x70,
								    &val,
								    error))
			return FALSE;
		self->superio_disabled = BIT_IS_SET(val, 20);
	} else if (rev == FU_ASPEED_AST2600) {
		if (!fu_aspeed_ast2x00_native_device_setup_read_u32(self,
								    AST_SOC_IO_SCU + 0xD8,
								    &val,
								    error))
			return FALSE;
		self->ilpc_disabled = BIT_IS_SET(val, 1);
		if (!fu_aspeed_ast2x00_native_device_setup_read_u32(self,
								    AST_SOC_IO_SCU + 0x510,
								    &val,
								    error))
			return FALSE;
		self->superio_disabled = BIT_IS_SET(val, 3);
		self->debug_disabled = BIT_IS_SET(val, 4);
	}

	if (!fu_aspeed_ast2x00_native_device_setup_read_u32(self,
							    AST_SOC_IO_LPC + 0x100,
							    &val,
							    error))
		return FALSE;
	self->ilpc_readonly = BIT_IS_SET(val, 6);

	if (rev == FU_ASPEED_AST2400) {
		/* debug UART is apparently not present in the AST2400 */
		self->debug_uart_disabled = TRUE;
	} else if (rev == FU_ASPEED_AST2500) {
		if (!fu_aspeed_ast2x00_native_device_setup_read_u32(self,
								    AST_SOC_IO_SCU + 0x2C,
								    &val,
								    error))
			return FALSE;
		self->debug_uart_disabled = BIT_IS_SET(val, 10);
	} else if (rev == FU_ASPEED_AST2600) {
		if (!fu_aspeed_ast2x00_native_device_setup_read_u32(self,
								    AST_SOC_IO_SCU + 0xC8,
								    &val,
								    error))
			return FALSE;
		if (!fu_aspeed_ast2x00_native_device_setup_read_u32(self,
								    AST_SOC_IO_SCU + 0xD8,
								    &val2,
								    error))
			return FALSE;
		self->debug_uart_disabled = BIT_IS_SET(val, 1) && BIT_IS_SET(val2, 3);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_aspeed_ast2x00_native_device_setup(FuDevice *device, GError **error)
{
	FuAspeedAst2x00NativeDevice *self = FU_ASPEED_AST2X00_NATIVE_DEVICE(device);

	if (!fu_aspeed_ast2x00_native_device_setup_xx1(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_aspeed_ast2x00_native_device_open(FuDevice *device, GError **error)
{
	FuAspeedAst2x00NativeDevice *self = FU_ASPEED_AST2X00_NATIVE_DEVICE(device);

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS(fu_aspeed_ast2x00_native_device_parent_class)->open(device, error))
		return FALSE;

	/* map region into memory */
	self->io = mmap(NULL,
			AST_SOC_IO_LEN,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			fu_udev_device_get_fd(FU_UDEV_DEVICE(device)),
			AST_SOC_IO);
	if (self->io == (gpointer)-1) {
		g_set_error(error,
			    G_IO_ERROR,
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED,
#endif
			    "failed to mmap %s: %s",
			    fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)),
			    strerror(errno));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_aspeed_ast2x00_native_device_close(FuDevice *device, GError **error)
{
	FuAspeedAst2x00NativeDevice *self = FU_ASPEED_AST2X00_NATIVE_DEVICE(device);

	/* unmap region */
	if (self->io != NULL) {
		munmap(self->io, AST_SOC_IO_LEN);
		self->io = NULL;
	}

	/* FuUdevDevice->close */
	return FU_DEVICE_CLASS(fu_aspeed_ast2x00_native_device_parent_class)->open(device, error);
}

static void
fu_aspeed_ast2x00_native_device_ilpc2ahb_readonly(FuAspeedAst2x00NativeDevice *self,
						  FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_ASPEED_ILPC2AHB_READONLY);
	fu_security_attrs_append(attrs, attr);

	/* success */
	if (self->ilpc_readonly) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		return;
	}

	/* failed */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
}

static void
fu_aspeed_ast2x00_native_device_ilpc2ahb_readwrite(FuAspeedAst2x00NativeDevice *self,
						   FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_ASPEED_ILPC2AHB_READWRITE);
	fu_security_attrs_append(attrs, attr);

	/* success */
	if (self->ilpc_disabled || self->superio_disabled) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		return;
	}

	/* success */
	if (self->ilpc_readonly) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
		return;
	}

	/* failed */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
}

static void
fu_aspeed_ast2x00_native_device_uart_debug(FuAspeedAst2x00NativeDevice *self,
					   FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr =
	    fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_ASPEED_UART_DEBUG);
	fu_security_attrs_append(attrs, attr);

	/* success */
	if (self->debug_uart_disabled) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		return;
	}

	/* failed */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_aspeed_ast2x00_native_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuAspeedAst2x00NativeDevice *self = FU_ASPEED_AST2X00_NATIVE_DEVICE(device);
	fu_aspeed_ast2x00_native_device_ilpc2ahb_readonly(self, attrs);
	fu_aspeed_ast2x00_native_device_ilpc2ahb_readwrite(self, attrs);
	fu_aspeed_ast2x00_native_device_uart_debug(self, attrs);
}

static void
fu_aspeed_ast2x00_native_device_init(FuAspeedAst2x00NativeDevice *self)
{
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(self), "/dev/mem");
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_OPEN_SYNC);
}

static void
fu_aspeed_ast2x00_native_device_class_init(FuAspeedAst2x00NativeDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_aspeed_ast2x00_native_device_to_string;
	klass_device->setup = fu_aspeed_ast2x00_native_device_setup;
	klass_device->open = fu_aspeed_ast2x00_native_device_open;
	klass_device->close = fu_aspeed_ast2x00_native_device_close;
	klass_device->add_security_attrs = fu_aspeed_ast2x00_native_device_add_security_attrs;
}
