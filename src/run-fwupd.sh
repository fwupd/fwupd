#!/usr/bin/sh
sudo \
	LIBFWUP_ESRT_DIR="../../fwupdate/linux/sys/firmware/efi/esrt/"	\
	FWUPD_RPI_FW_DIR="../data/tests/rpiboot" 			\
	FWUPD_ENABLE_TEST_PLUGIN="1"					\
	G_MESSAGES_DEBUG="all"						\
	./fwupd --verbose
