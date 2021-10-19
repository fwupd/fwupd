install_data([
    'wacom.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-wacom-usb',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'wacom.builder.xml'),
     join_paths(meson.current_source_dir(), 'wacom.wac'),
  ],
)
