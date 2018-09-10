build:
	true
install:
	#fixes up fwupdtool -> fwupd.fwupdtool
	sed -i "s,\(complete -F _fwupd[a-z]*\) \(fwupd.*\),\1 fwupd.\2,; 		\
		s,\(command.*\)\(fwupdtool\),\1fwupd.\2," 				\
			${SNAPCRAFT_STAGE}/share/bash-completion/completions/*
	# fixes up dbus service for classic snap
	sed -i 's!SystemdService=\(.*\)!SystemdService=snap.fwupd.fwupd.service!' \
		${SNAPCRAFT_STAGE}/share/dbus-1/system-services/org.freedesktop.fwupd.service
