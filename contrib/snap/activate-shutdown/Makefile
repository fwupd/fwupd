build:
	true
install:
	install -d ${DESTDIR}/etc/systemd/system/
	install -m0644 fwupd-activate.service ${DESTDIR}/etc/systemd/system
	# fixes up shutdown activation script for classic snap
	sed -i "s,/libexec/fwupd/,/snap/bin/fwupd.,"                                    \
		${SNAPCRAFT_STAGE}/lib/systemd/system-shutdown/fwupd.shutdown

