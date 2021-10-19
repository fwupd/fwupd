install_data([
    'cros-ec.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-cros-ec',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'cros-ec.builder.xml'),
     join_paths(meson.current_source_dir(), 'cros-ec.bin'),
  ],
)
