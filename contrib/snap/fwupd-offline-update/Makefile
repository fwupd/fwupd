build:
	true
install:
	install -d ${DESTDIR}/etc/systemd/system/
	install -m0644 ${SNAPCRAFT_STAGE}/lib/systemd/system/fwupd-offline-update.service ${DESTDIR}/etc/systemd/system
	# fixes up offline conditions for snap
	sed -i "/^ConditionPathExists.*/d; s,/libexec/fwupd/,/snap/bin/fwupd.,; s,After=.*,& snapd.socket," \
		${DESTDIR}/etc/systemd/system/fwupd-offline-update.service

