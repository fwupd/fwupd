if honggfuzz.found() and get_option('tests')
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
