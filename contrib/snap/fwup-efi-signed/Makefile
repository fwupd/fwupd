DEB_HOST_ARCH=$(shell dpkg-architecture -q DEB_HOST_ARCH)
EFI_NAME := UNKNOWN-EFI-NAME

ifeq ($(DEB_HOST_ARCH),amd64)
EFI_NAME := x64
endif

ifeq ($(DEB_HOST_ARCH),i386)
EFI_NAME := ia32
endif

ifeq ($(DEB_HOST_ARCH),arm64)
EFI_NAME := aa64
endif

ifeq ($(DEB_HOST_ARCH),armhf)
EFI_NAME := arm
endif

SIGNED := \
	fwup$(EFI_NAME).efi.signed

all: $(SIGNED)

$(SIGNED):
	./download-fwupdate
	mv version $(SIGNED).version

install: $(SIGNED)
	install -d $(DESTDIR)/usr/lib/fwupd/efi
	install -m0644 $(SIGNED) $(SIGNED).version \
		$(DESTDIR)/usr/lib/fwupd/efi

clean:
	rm -f $(SIGNED) $(SIGNED).version
