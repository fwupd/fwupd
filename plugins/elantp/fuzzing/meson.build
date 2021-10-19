install_data([
    'elantp.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-elantp',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'elantp.builder.xml'),
     join_paths(meson.current_source_dir(), 'elantp.bin'),
  ],
)
