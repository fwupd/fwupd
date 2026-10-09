cargo_env = environment()
cargo_env.set('MESON_BUILDTYPE', get_option('buildtype'))
cargo_env.set('MESON_BUILD_ROOT', meson.project_build_root())
cargo_env.set('WORKSPACE_CARGO_TOML', join_paths(meson.project_source_root(), 'Cargo.toml'))
cargo_env.set('CARGO', cargo.full_path())
if meson.is_cross_build()
  rust_target = meson.get_external_property('rust_target', '')
  if rust_target != ''
    cargo_env.set('MESON_RUST_TARGET', rust_target)
  endif
endif

fwupd_rust_src = [
  join_paths(meson.project_source_root(), 'Cargo.toml'),
  join_paths(meson.project_source_root(), 'Cargo.lock'),
  'fwupd/Cargo.toml',
  'fwupd/src/bitflags.rs',
  'fwupd/src/compressor/mod.rs',
  'fwupd/src/compressor/zlib.rs',
  'fwupd/src/json/array.rs',
  'fwupd/src/json/error.rs',
  'fwupd/src/json/mod.rs',
  'fwupd/src/json/node.rs',
  'fwupd/src/json/object.rs',
  'fwupd/src/json/parser.rs',
  'fwupd/src/json/tests.rs',
  'fwupd/src/lib.rs',
  'fwupd/src/streams/composite_input_stream.rs',
  'fwupd/src/streams/compressor_stream.rs',
  'fwupd/src/streams/file_input_stream.rs',
  'fwupd/src/streams/memory_input_stream.rs',
  'fwupd/src/streams/mod.rs',
  'fwupd/src/streams/partial_input_stream.rs',
  'fwupd/src/units.rs',
]

fwupd_ffi_src = [
  join_paths(meson.project_source_root(), 'Cargo.toml'),
  join_paths(meson.project_source_root(), 'Cargo.lock'),
  'fwupd-ffi/Cargo.toml',
  'fwupd-ffi/src/compressor.rs',
  'fwupd-ffi/src/cstream.rs',
  'fwupd-ffi/src/glib/gerror.rs',
  'fwupd-ffi/src/glib/grefstring.rs',
  'fwupd-ffi/src/glib/gstring.rs',
  'fwupd-ffi/src/glib/mod.rs',
  'fwupd-ffi/src/json/array.rs',
  'fwupd-ffi/src/json/mod.rs',
  'fwupd-ffi/src/json/node.rs',
  'fwupd-ffi/src/json/object.rs',
  'fwupd-ffi/src/json/parser.rs',
  'fwupd-ffi/src/lib.rs',
  'fwupd-ffi/src/streams.rs',
]

# The pure Rust library
fwupd_rust_lib = custom_target(
  'fwupd-rust',
  output: 'fwupd-rust.stamp',
  command: [cargo_wrapper, '-p', 'fwupd', 'build', '--stamp', '@OUTPUT@'],
  env: cargo_env,
  console: true,
  install: false,
  build_by_default: false,
  depend_files: fwupd_rust_src,
)

# The FFI lib to call into Rust
fwupd_ffi_lib = custom_target(
  'fwupd-ffi',
  output: 'libfwupd_ffi.a',
  command: [cargo_wrapper, '-p', 'fwupd-ffi', 'build', '--output', '@OUTPUT@'],
  env: cargo_env,
  console: true,
  install: host_machine.system() == 'android',
  install_dir: get_option('libdir'),
  build_by_default: false,
  depend_files: fwupd_ffi_src,
  depends: [fwupd_rust_lib],
)

fwupd_rust_ffi_incdir = include_directories('fwupd-ffi')

# Rust's std requires system libraries that must be named explicitly when a C linker
# links the static archive (cargo normally handles this automatically).
rust_ffi_extra_deps = []
if host_machine.system() == 'windows'
  foreach lib : ['ws2_32', 'userenv', 'bcrypt', 'ntdll']
    rust_ffi_extra_deps += cc.find_library(lib)
  endforeach
else
  rust_ffi_extra_deps += dependency('threads')
endif

fwupd_rust_ffi_dep = declare_dependency(
  # We can't use link_with here because custom_target output
  # isn't accepted. So all users of our crate need an explicit
  # link_with in addition to the dependencies
  include_directories: [fwupd_rust_ffi_incdir],
  dependencies: [zlib] + rust_ffi_extra_deps,
)

if build_docs
  custom_target(
    'rust-doc',
    output: 'doc',
    command: [cargo_wrapper, 'doc', '--output', '@OUTPUT@', '--no-deps'],
    env: cargo_env,
    console: true,
    install: false,
    build_by_default: true,
    depend_files: [fwupd_rust_src, fwupd_ffi_src],
  )
endif

if get_option('tests') and not meson.is_cross_build()
  test(
    'fwupd-rust',
    cargo_wrapper,
    args: ['-p', 'fwupd', 'test'],
    env: cargo_env,
    timeout: 120,
    suite: 'rust',
    depends: [fwupd_rust_lib],
  )
  test(
    'fwupd-rust-ffi',
    cargo_wrapper,
    args: ['-p', 'fwupd-ffi', 'test'],
    env: cargo_env,
    timeout: 120,
    suite: 'rust',
    depends: [fwupd_ffi_lib],
  )
  if build_docs
    test(
      'fwupd-doctest',
      cargo_wrapper,
      args: ['-p', 'fwupd', 'test', '--doc'],
      env: cargo_env,
      timeout: 120,
      suite: 'rust',
      depends: [fwupd_rust_lib],
    )
  endif
endif
