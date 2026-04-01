# Build the fwupd Rust crates using meson's native Cargo workspace integration.

# Get the package objects from the workspace
fwupd_pkg = cargo_ws.package('fwupd')
fwupd_ffi_pkg = cargo_ws.package('fwupd-ffi')

# Build the pure Rust library.
fwupd_rust_lib = fwupd_pkg.library(install: false)
fwupd_pkg.override_dependency(
  declare_dependency(link_with: fwupd_rust_lib),
)

# Build the FFI static library that exposes extern "C" functions.
fwupd_rs_lib = fwupd_ffi_pkg.library(
  rust_abi: 'c',
  install: false,
)

# Unit tests for the pure Rust library
rustmod.test(
  'fwupd-rust',
  fwupd_rust_lib,
  timeout: 120,
  suite: 'rust',
)

if build_docs
  rustmod.doctest(
    'fwupd-doctest',
    fwupd_rust_lib,
    timeout: 120,
    suite: 'rust',
  )
endif
