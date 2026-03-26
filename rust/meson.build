core_cargo_env = cargo_env
core_cargo_env.set('MESON_SOURCE_DIR', join_paths(meson.current_source_dir()))

fwupd_rs_files = files(
  'Cargo.toml',
  'Cargo.lock',
  'fwupd/Cargo.toml',
  'fwupd/src/lib.rs',
)

# Force a build so we pick up any issues during compile
fwupd_rust = custom_target(
  'fwupd-rust',
  output: 'fwupd-rust.stamp',
  command: [
    cargo_wrapper,
    '-p', 'fwupd',
    '--stamp',
    'build',
    '@OUTPUT@',
  ],
  env: core_cargo_env,
  depend_files: fwupd_rs_files,
  console: true,
  install: false,
  build_by_default: true,
)

test(
  'cargo test',
  cargo_wrapper,
  args: ['test'],
  env: core_cargo_env,
  timeout: 120,
  suite: 'rust',
  depends: [fwupd_rust],
)

if build_docs
  custom_target(
    'rust-doc',
    output: 'doc',
    command: [cargo_wrapper, 'doc', '@OUTPUT@'],
    env: core_cargo_env,
    depend_files: [
      fwupd_rs_files,
    ],
    console: true,
    install: false,
    build_by_default: true,
  )
endif
