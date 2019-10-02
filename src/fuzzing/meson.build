run_target('fuzz-smbios',
  command: [
    join_paths(meson.source_root(), 'contrib/afl-fuzz.py'),
    '-i', join_paths(meson.current_source_dir(), 'smbios'),
    '-o', join_paths(meson.current_build_dir(), '..', 'findings-smbios'),
    '--command', 'smbios-dump',
    fwupdtool,
  ],
)
run_target('fuzz-firmware',
  command: [
    join_paths(meson.source_root(), 'contrib/afl-fuzz.py'),
    '-i', join_paths(meson.current_source_dir(), 'firmware'),
    '-o', join_paths(meson.current_build_dir(), '..', 'findings-firmware'),
    fwupd_firmware_dump,
  ],
)
