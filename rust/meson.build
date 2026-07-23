fwupd_pkg = cargo_ws.package('fwupd')
fwupd_ffi_pkg = cargo_ws.package('fwupd-ffi')

# The pure Rust library
fwupd_rust_lib = fwupd_pkg.library(install: false)
fwupd_pkg.override_dependency(
  declare_dependency(link_with: fwupd_rust_lib),
)

# The FFI lib to call into Rust
fwupd_ffi_lib = fwupd_ffi_pkg.library(
  rust_abi: 'c',
  install: false,
)

fwupd_rust_ffi_incdir = include_directories('fwupd-ffi')

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
