# Build the fwupd Rust crates using meson's native Cargo workspace integration.

# Get the package objects from the workspace
fwupd_pkg = cargo_ws.package('fwupd')

# Build the pure Rust library.
fwupd_rust_lib = fwupd_pkg.library(install: false)

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
