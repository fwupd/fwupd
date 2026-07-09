fwupd_pkg = cargo_ws.package('fwupd')
fwupd_ffi_pkg = cargo_ws.package('fwupd-ffi')

# Abort instead of trying to unwind through C FFI which is undefined behavior
# This cannot be set in tests though unless we switch to nightly.
#    error: building tests with panic=abort is not supported without `-Zpanic_abort_tests`
rust_panic_abort_flags = ['-Cpanic=abort']
rust_panic_unwind_flags = ['-Cpanic=unwind']

# The pure Rust library
fwupd_rust_lib = fwupd_pkg.library(
  rust_args: rust_panic_abort_flags,
  install: false
)
fwupd_pkg.override_dependency(
  declare_dependency(link_with: fwupd_rust_lib),
)

# The FFI lib to call into Rust
fwupd_ffi_lib = fwupd_ffi_pkg.library(
  rust_args: rust_panic_abort_flags,
  rust_abi: 'c',
  install: false,
)

fwupd_rust_ffi_incdir = include_directories('fwupd-ffi')

if get_option('tests')
  rustmod.test(
    'fwupd-rust',
    fwupd_rust_lib,
    rust_args: rust_panic_unwind_flags,
    timeout: 120,
    suite: 'rust',
  )

  if build_docs
    rustmod.doctest(
      'fwupd-doctest',
      fwupd_rust_lib,
      rust_args: rust_panic_unwind_flags,
      timeout: 120,
      suite: 'rust',
    )
  endif
endif
