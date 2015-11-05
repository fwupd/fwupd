#!/usr/bin/sh
sudo \
	LIBFWUP_ESRT_DIR="../../fwupdate/linux/sys/firmware/efi/esrt/"	\
	FWUPD_RPI_FW_DIR="../data/tests/rpiboot" 			\
	G_MESSAGES_DEBUG="all"						\
	./fwupd --verbose
