install_data([
    'ccgx.builder.xml',
    'ccgx-dmc.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-ccgx',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'ccgx.builder.xml'),
     join_paths(meson.current_source_dir(), 'ccgx.cyacd'),
  ],
)
run_target('fuzz-targets-ccgx-dmc',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'ccgx-dmc.builder.xml'),
     join_paths(meson.current_source_dir(), 'ccgx-dmc.bin'),
  ],
)
