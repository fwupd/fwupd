install_data([
    'pixart.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-pixart',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'pixart.builder.xml'),
     join_paths(meson.current_source_dir(), 'pixart.bin'),
  ],
)
