build:
	true
install:
	#fixes up fwupdtool -> fwupd.fwupdtool
	sed -i "s,\(complete -F _fwupd[a-z]*\) \(fwupd.*\),\1 fwupd.\2,; 		\
		s,\(command.*\)\(fwupdtool\),\1fwupd.\2," 				\
			${SNAPCRAFT_STAGE}/usr/share/bash-completion/completions/*
