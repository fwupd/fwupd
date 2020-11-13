run_target('fuzz-optionrom',
  command: [
    join_paths(meson.source_root(), 'contrib/afl-fuzz.py'),
    '--command', 'rom',
    '-i', meson.current_source_dir(),
    '-o', join_paths(meson.current_build_dir(), '..', 'findings'),
    optionrom_tool,
  ],
)
