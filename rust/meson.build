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

if enable_rust_plugins
  # ABI/constant validation tests for the -sys crates.
  # These compile C code to cross-validate struct layouts and constant values
  # against the Rust FFI bindings.
  test(
    'fwupd-sys-abi',
    cargo_wrapper,
    args: ['-p', 'fwupd-sys', 'test'],
    env: cargo_env,
    depends: [fwupd],
    timeout: 120,
    suite: 'rust',
  )
  test(
    'fwupdplugin-sys-abi',
    cargo_wrapper,
    args: ['-p', 'fwupdplugin-sys', 'test'],
    env: cargo_env,
    depends: [fwupdplugin],
    timeout: 120,
    suite: 'rust',
  )
  test(
    'fwupdplugin',
    cargo_wrapper,
    args: ['-p', 'fwupdplugin', 'test'],
    env: cargo_env,
    depends: [fwupdplugin],
    timeout: 120,
    suite: 'rust',
  )
  # Build the example plugin as a smoke test that everything compiles.
  # The output .so follows the fwupd naming convention: libfu_plugin_<name>.so
  #
  # We use 'sh -c' to run cargo and then copy the output to where meson
  # expects it, since cargo writes to its own target directory layout.
  #
  # fwupd expects plugins in builddir/plugins/<name>/... so for now
  # we separate the build job and have a separate copy job. The need for this goes away once rust code
  # is promoted to be in-tree instead of within the rust/ directory.
  rust_example_plugin = custom_target(
    'rust-plugin-example',
    output: 'libfu_plugin_example.so',
    command: [
      cargo_wrapper,
      '-p', 'fu-plugin-example',
      '--copy', join_paths('plugins', 'example'),
      'build',
      '@OUTPUT@',
    ],
    env: cargo_env,
    depends: [fwupdplugin],
    depend_files: [
      files(
        'Cargo.toml',
        'Cargo.lock',
        'fwupdplugin/Cargo.toml',
        'fwupdplugin/src/lib.rs',
        'fwupdplugin/src/plugin.rs',
        'fwupdplugin/src/device.rs',
        'fwupdplugin/src/device_subclass.rs',
        'fwupdplugin/src/firmware.rs',
        'fwupdplugin/src/firmware_subclass.rs',
        'fwupdplugin/src/context.rs',
        'fwupdplugin/src/progress.rs',
        'fwupdplugin/src/udev_device.rs',
        'fwupdplugin/src/usb_device.rs',
        'fwupdplugin/src/hid_device.rs',
        'fwupdplugin-sys/Cargo.toml',
        'fwupdplugin-sys/src/lib.rs',
        'fwupd-sys/Cargo.toml',
        'fwupd-sys/src/lib.rs',
        'plugins/fu-plugin-example/Cargo.toml',
        'plugins/fu-plugin-example/src/lib.rs',
      ),
    ],
    console: true,
    install: false,
    build_by_default: true,
  )
endif
