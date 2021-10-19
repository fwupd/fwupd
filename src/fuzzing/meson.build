install_data([
    'cfu-offer.builder.xml',
    'cfu-payload.builder.xml',
    'efi-firmware-file.builder.xml',
    'efi-firmware-filesystem.builder.xml',
    'efi-firmware-volume.builder.xml',
    'fmap.builder.xml',
    'fmap-offset.builder.xml',
    'ifd-bios.builder.xml',
    'ifd.builder.xml',
    'ifd-no-bios.builder.xml',
    'ihex.builder.xml',
    'srec-addr32.builder.xml',
    'srec.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-cfu-offer',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'cfu-offer.builder.xml'),
     join_paths(meson.current_source_dir(), 'cfu.offer'),
  ],
)
run_target('fuzz-targets-cfu-payload',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'cfu-payload.builder.xml'),
     join_paths(meson.current_source_dir(), 'cfu.payload'),
  ],
)
run_target('fuzz-targets-efi-firmware-file',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'efi-firmware-file.builder.xml'),
     join_paths(meson.current_source_dir(), 'efi-file.bin'),
  ],
)
run_target('fuzz-targets-efi-firmware-filesystem',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'efi-firmware-filesystem.builder.xml'),
     join_paths(meson.current_source_dir(), 'efi-filesystem.bin'),
  ],
)
run_target('fuzz-targets-efi-firmware-volume',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'efi-firmware-volume.builder.xml'),
     join_paths(meson.current_source_dir(), 'efi-volume.bin'),
  ],
)
run_target('fuzz-targets-fmap',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'fmap.builder.xml'),
     join_paths(meson.current_source_dir(), 'fmap.bin'),
  ],
)
run_target('fuzz-targets-fmap-offset',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'fmap-offset.builder.xml'),
     join_paths(meson.current_source_dir(), 'fmap-offset.bin'),
  ],
)
run_target('fuzz-targets-ifd-bios',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'ifd-bios.builder.xml'),
     join_paths(meson.current_source_dir(), 'ifd-bios.bin'),
  ],
)
run_target('fuzz-targets-ifd',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'ifd.builder.xml'),
     join_paths(meson.current_source_dir(), 'ifd.bin'),
  ],
)
run_target('fuzz-targets-ifd-no-bios',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'ifd-no-bios.builder.xml'),
     join_paths(meson.current_source_dir(), 'ifd-no-bios.bin'),
  ],
)
run_target('fuzz-targets-ihex',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'ihex.builder.xml'),
     join_paths(meson.current_source_dir(), 'ihex.hex'),
  ],
)
run_target('fuzz-targets-srec-addr32',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'srec-addr32.builder.xml'),
     join_paths(meson.current_source_dir(), 'srec-addr32.srec'),
  ],
)
run_target('fuzz-targets-srec',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'srec.builder.xml'),
     join_paths(meson.current_source_dir(), 'srec.srec'),
  ],
)
