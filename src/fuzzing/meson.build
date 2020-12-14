if honggfuzz.found()
  run_target('fuzz-smbios',
    command: [
      honggfuzz,
      '--input', join_paths(meson.current_source_dir(), 'smbios'),
      '--output', join_paths(meson.current_build_dir(), 'smbios-corpus'),
      '--workspace', join_paths(meson.current_build_dir(), 'smbios-findings'),
      '--verifier',
      '--rlimit_rss', '10',
      '--', fwupdtool, 'smbios-dump', '___FILE___',
    ],
  )
if get_option('tests')
  run_target('fuzz-firmware',
    command: [
      honggfuzz,
      '--input', join_paths(meson.current_source_dir(), 'firmware'),
      '--output', join_paths(meson.current_build_dir(), 'firmware-corpus'),
      '--workspace', join_paths(meson.current_build_dir(), 'firmware-findings'),
      '--verifier',
      '--rlimit_rss', '10',
      '--timeout', '5', '-P', '--',
      fwupd_firmware_dump, '--timeout', '50',
    ],
)
endif
endif
