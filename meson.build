project('fwupd', 'c',
  version: '2.0.0',
  license: 'LGPL-2.1-or-later',
  meson_version: '>=0.62.0',
  default_options: ['warning_level=2', 'c_std=c11'],
)

fwupd_version = meson.project_version()
varr = fwupd_version.split('.')
fwupd_major_version = varr[0]
fwupd_minor_version = varr[1]
fwupd_micro_version = varr[2]

conf = configuration_data()
conf.set('MAJOR_VERSION', fwupd_major_version)
conf.set('MINOR_VERSION', fwupd_minor_version)
conf.set('MICRO_VERSION', fwupd_micro_version)
conf.set_quoted('PACKAGE_VERSION', fwupd_version)

# get source version, falling back to package version
git = find_program('git', required: false)
tag = false
if git.found()
  source_version = run_command([git, 'describe'], check: false).stdout().strip()
  if source_version == ''
    source_version = fwupd_version
  endif
  tag = run_command([git, 'describe', '--exact-match'], check: false).returncode() == 0
else
  source_version = fwupd_version
endif
conf.set_quoted('SOURCE_VERSION', source_version)

# libtool versioning - this applies to libfwupd
#
# See http://sources.redhat.com/autobook/autobook/autobook_91.html#SEC91 for details
#
# - If interfaces have been changed or added, but binary compatibility
#   has been preserved, change:
#      CURRENT += 1
#      REVISION = 0
#      AGE += 1
# - If binary compatibility has been broken (eg removed or changed
#   interfaces), change:
#      CURRENT += 1
#      REVISION = 0
#      AGE = 0
# - If the interface is the same as the previous version, but bugs are
#   fixed, change:
#      REVISION += 1
libfwupd_lt_current = '3'
libfwupd_lt_revision = '0'
libfwupd_lt_age = '0'
libfwupd_lt_version = '@0@.@1@.@2@'.format(libfwupd_lt_current, libfwupd_lt_age, libfwupd_lt_revision)

# get supported warning flags
warning_flags = [
  '-Wfatal-errors',
  '-Waggregate-return',
  '-Wunused',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wdiscarded-qualifiers',
  '-Wduplicated-branches',
  '-Wduplicated-cond',
  '-Wempty-body',
  '-Wfloat-equal',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Wimplicit-int',
  '-Wincompatible-pointer-types',
  '-Winit-self',
  '-Wint-conversion',
  '-Wlogical-op',
  '-Wmaybe-uninitialized',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-cast-function-type',
  '-Wno-address-of-packed-member',  # incompatible with g_autoptr()
  '-Wno-unknown-pragmas',
  '-Wno-missing-field-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-typedef-redefinition',
  '-Wno-unknown-warning-option',
  '-Wno-unused-parameter',
  '-Wno-nonnull-compare',
  '-Wno-analyzer-use-of-uninitialized-value', # incompatible with g_autoptr()
  '-Wno-analyzer-fd-double-close',
  '-Wold-style-definition',
  '-Woverride-init',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wunused-variable',
  '-Wvla',
  '-Wwrite-strings'
]
static_analysis = get_option('static_analysis') and host_machine.system() != 'windows'
if static_analysis
  warning_flags += ['-fanalyzer', '-Wno-analyzer-null-dereference']
endif
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(warning_flags), language: 'c')

if not meson.is_cross_build()
  add_project_arguments('-fstack-protector-strong', language: 'c')
endif

if cc.get_id() == 'msvc'
  error('MSVC is not supported as it does not support __attribute__((cleanup))')
endif

# enable full RELRO where possible
# FIXME: until https://github.com/mesonbuild/meson/issues/1140 is fixed
global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,defs',
  '-Wl,-z,now',
  '-Wl,-z,ibt,-z,shstk',
]
foreach arg: test_link_args
  if cc.has_link_argument(arg)
    global_link_args += arg
  endif
endforeach
add_project_link_arguments(
  global_link_args,
  language: 'c'
)

add_project_arguments('-DFWUPD_COMPILATION', language: 'c')

# Needed for realpath(), syscall(), cfmakeraw(), etc.
add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

# needed for symlink() and BYTE_ORDER
add_project_arguments('-D_BSD_SOURCE', language: 'c')
add_project_arguments('-D__BSD_VISIBLE', language: 'c')
add_project_arguments('-D_XOPEN_SOURCE=700', language: 'c')

# needed for memfd_create()
add_project_arguments('-D_GNU_SOURCE', language: 'c')

# needed for memmem()
add_project_arguments('-D_DARWIN_C_SOURCE=900000', language: 'c')

# sanity check
if get_option('build') == 'all'
  build_standalone = true
  build_daemon = true
elif get_option('build') == 'standalone'
  build_standalone = true
  build_daemon = false
elif get_option('build') == 'library'
  build_standalone = false
  build_daemon = false
endif

prefix = get_option('prefix')

bindir = join_paths(prefix, get_option('bindir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
#this ends up in compiled code, ignore prefix
if host_machine.system() == 'windows'
  sysconfdir = get_option('sysconfdir')
  localstatedir = get_option('localstatedir')
  datadir = get_option('datadir')
  installed_test_bindir = get_option('libexecdir')
  installed_test_datadir = get_option('datadir')
  daemon_dir = get_option('libexecdir')
else
  datadir = join_paths(prefix, get_option('datadir'))
  sysconfdir = join_paths(prefix, get_option('sysconfdir'))
  localstatedir = join_paths(prefix, get_option('localstatedir'))
  installed_test_bindir = join_paths(libexecdir, 'installed-tests', meson.project_name())
  installed_test_datadir = join_paths(datadir, 'installed-tests', meson.project_name())
  daemon_dir = join_paths(libexecdir, 'fwupd')
endif
mandir = join_paths(prefix, get_option('mandir'))
localedir = join_paths(prefix, get_option('localedir'))

diffcmd = find_program('diff')
gio = dependency('gio-2.0', version: '>= 2.68.0')
giounix = dependency('gio-unix-2.0', version: '>= 2.68.0', required: false)
if giounix.found()
  conf.set('HAVE_GIO_UNIX', '1')
endif
gmodule = dependency('gmodule-2.0')
if build_standalone
gudev = dependency('gudev-1.0', version: '>= 232',
                    required: get_option('gudev').disable_auto_if(host_machine.system() != 'linux'))
if gudev.found()
  conf.set('HAVE_GUDEV', '1')
endif
bluez = get_option('bluez').disable_auto_if(host_machine.system() != 'linux')
if bluez.allowed()
  conf.set('HAVE_BLUEZ', '1')
endif
host_cpu = host_machine.cpu_family()
hsi = get_option('hsi').disable_auto_if(host_machine.system() != 'linux').disable_auto_if(host_cpu != 'x86' and host_cpu != 'x86_64').allowed()
if hsi
  conf.set('HAVE_HSI', '1')
endif
libxmlb = dependency('xmlb', version: '>= 0.3.18', fallback: ['libxmlb', 'libxmlb_dep'])
if libxmlb.get_variable('zstd') == 'true'
  lvfs_metadata_format = 'zst'
elif libxmlb.get_variable('lzma') == 'true'
  lvfs_metadata_format = 'xz'
else
  lvfs_metadata_format = 'gz'
endif
conf.set_quoted('FU_LVFS_METADATA_FORMAT', lvfs_metadata_format)

# FreeBSD is missing some libusb symbols
libusb = dependency('libusb-1.0', version : '>= 0.1.12')
conf.set_quoted('LIBUSB_VERSION', libusb.version())
if cc.has_header_symbol('libusb.h', 'libusb_set_option', dependencies: libusb)
  conf.set('HAVE_LIBUSB_SET_OPTION', '1')
endif
if cc.has_header_symbol('libusb.h', 'libusb_get_parent', dependencies: libusb)
  conf.set('HAVE_LIBUSB_GET_PARENT', '1')
endif

sqlite = dependency('sqlite3', required: get_option('sqlite'))
if sqlite.found()
  conf.set('HAVE_SQLITE', '1')
endif
passim = dependency('passim', version: '>= 0.1.5', required: get_option('passim'), fallback: ['passim', 'passim_dep'])
if passim.found()
  conf.set('HAVE_PASSIM', '1')
endif
libarchive = dependency('libarchive', required: get_option('libarchive'))
if libarchive.found()
  conf.set('HAVE_LIBARCHIVE', '1')
  if cc.has_header_symbol('archive.h', 'archive_write_add_filter_zstd')
    conf.set('HAVE_LIBARCHIVE_WRITE_ADD_FILTER_ZSTD', '1')
  endif
endif
endif
libjcat = dependency('jcat', version: '>= 0.2.0', fallback: ['libjcat', 'libjcat_dep'])
libjsonglib = dependency('json-glib-1.0', version: '>= 1.6.0')
valgrind = dependency('valgrind', required: false)
libcurl = dependency('libcurl', version: '>= 7.62.0', required: get_option('curl'))
if libcurl.found()
  conf.set('HAVE_LIBCURL', '1')
endif
polkit = dependency('polkit-gobject-1', version: '>= 0.103',
         required: get_option('polkit').disable_auto_if(host_machine.system() != 'linux'))
if polkit.found()
  conf.set('HAVE_POLKIT', '1')
  if polkit.version().version_compare('>= 0.114')
    conf.set('HAVE_POLKIT_0_114', '1')
  endif
  conf.set_quoted ('POLKIT_ACTIONDIR', polkit.get_variable(pkgconfig: 'actiondir'))
endif
if build_daemon
  if not polkit.found()
    warning('Polkit is disabled, the daemon will allow ALL client actions')
  endif
endif
libm = cc.find_library('m', required: false)
zlib = dependency('zlib')

fs = import('fs')

# look for usb.ids in both of the Debian and Fedora locations,
# and fall back to the system datadir in case we're building in a venv or prefix
vendor_ids_dir = get_option('vendor_ids_dir')
if vendor_ids_dir == ''
  vendor_ids_dir = join_paths(datadir, 'misc')
  if not fs.is_file(join_paths(vendor_ids_dir, 'usb.ids'))
    vendor_ids_dir = join_paths(datadir, 'hwdata')
  endif
  if not fs.is_file(join_paths(vendor_ids_dir, 'usb.ids'))
    vendor_ids_dir = '/usr/share/hwdata'
  endif
  if not fs.is_file(join_paths(vendor_ids_dir, 'usb.ids'))
    vendor_ids_dir = '/usr/share/misc'
  endif
  if not fs.is_file(join_paths(vendor_ids_dir, 'usb.ids'))
    error('could not auto-detect -Dvendor_ids_dir=')
  endif
endif
conf.set_quoted ('FWUPD_DATADIR_VENDOR_IDS', vendor_ids_dir)

bashcomp = dependency('bash-completion', required: false)
python3path = get_option('python')
if python3path == ''
  python3 = import('python').find_installation('python3')
else
  python3 = find_program(python3path)
endif

gnutls = dependency('gnutls', version: '>= 3.6.0', required: get_option('gnutls'))
if gnutls.found()
  conf.set('HAVE_GNUTLS', '1')
endif

libgbinder = dependency('libgbinder', version: '>= 1.1.0', required: get_option('binder'))
if libgbinder.found()
  conf.set('HAVE_BINDER', '1')
endif

lzma = dependency('liblzma', required: get_option('lzma'))
if lzma.found()
  conf.set('HAVE_LZMA', '1')
endif

cbor = dependency('libcbor', version: '>= 0.7.0', required: get_option('cbor'))
if cbor.found()
  conf.set('HAVE_CBOR', '1')
  if cc.has_header_symbol('cbor.h', 'cbor_set_allocs')
    conf.set('HAVE_CBOR_SET_ALLOCS', '1')
  endif
endif

platform_deps = []
if get_option('default_library') != 'static'
  if host_machine.system() == 'windows'
    platform_deps += cc.find_library('shlwapi')
  endif
  if host_machine.system() == 'freebsd'
    platform_deps += dependency('efivar')
  endif
endif

if valgrind.found()
  conf.set('HAVE_VALGRIND', '1')
endif

libsystemd = dependency('libsystemd',
                        required: get_option('systemd').disable_auto_if(host_machine.system() != 'linux'))
offline = get_option('offline')
if offline
  conf.set('HAVE_FWUPDOFFLINE', '1')
endif

if cc.has_header('sys/utsname.h')
  conf.set('HAVE_UTSNAME_H', '1')
endif
if cc.has_header('sys/inotify.h')
  conf.set('HAVE_INOTIFY_H', '1')
endif
if cc.has_header('sys/ioctl.h')
  conf.set('HAVE_IOCTL_H', '1')
endif
if cc.has_header('errno.h')
  conf.set('HAVE_ERRNO_H', '1')
endif
if cc.has_header('sys/socket.h')
  conf.set('HAVE_SOCKET_H', '1')
endif
if cc.has_header('scsi/sg.h')
  conf.set('HAVE_SCSI_SG_H', '1')
endif
if cc.has_header('sys/select.h')
  conf.set('HAVE_SELECT_H', '1')
endif
if cc.has_header('sys/io.h') and cc.has_function('outb', prefix: '#include <sys/io.h>')
  conf.set('HAVE_IO_H', '1')
endif
if cc.has_header('linux/ethtool.h')
  conf.set('HAVE_ETHTOOL_H', '1')
endif
if cc.has_header('linux/mei.h')
  conf.set('HAVE_MEI_H', '1')
endif
if cc.has_header('mtd/mtd-user.h')
  conf.set('HAVE_MTD_USER_H', '1')
endif
if cc.has_header('linux/hidraw.h')
  conf.set('HAVE_HIDRAW_H', '1')
endif
if cc.has_header('sys/mman.h')
  conf.set('HAVE_MMAN_H', '1')
endif
if cc.has_header('poll.h')
  conf.set('HAVE_POLL_H', '1')
endif
if cc.has_header('kenv.h')
  conf.set('HAVE_KENV_H', '1')
endif
if cc.has_header('malloc.h')
  conf.set('HAVE_MALLOC_H', '1')
  if cc.has_function('malloc_trim', prefix: '#include <malloc.h>')
	 conf.set('HAVE_MALLOC_TRIM', '1')
  endif
endif
has_cpuid = cc.has_header_symbol('cpuid.h', '__get_cpuid_count', required: get_option('plugin_msr'))
if has_cpuid
  conf.set('HAVE_CPUID_H', '1')
endif
if cc.has_function('getuid')
  conf.set('HAVE_GETUID', '1')
endif
if cc.has_function('realpath')
  conf.set('HAVE_REALPATH', '1')
endif
if cc.has_function('memmem')
  conf.set('HAVE_MEMMEM', '1')
endif
if cc.has_function('sigaction')
  conf.set('HAVE_SIGACTION', '1')
endif
if cc.has_function('memfd_create')
  conf.set('HAVE_MEMFD_CREATE', '1')
endif
if cc.has_header_symbol('locale.h', 'LC_MESSAGES')
  conf.set('HAVE_LC_MESSAGES', '1')
endif
if cc.has_header('linux/ipmi.h')
  have_linux_ipmi = true
  conf.set('HAVE_LINUX_IPMI_H', '1')
else
  have_linux_ipmi = false
endif
if cc.has_header_symbol('fcntl.h', 'F_WRLCK')
  conf.set('HAVE_WRLCK', '1')
endif
if cc.has_header_symbol('fcntl.h', 'F_OFD_SETLK')
  conf.set('HAVE_OFD', '1')
endif
if cc.has_function('pwrite', args: '-D_XOPEN_SOURCE')
  conf.set('HAVE_PWRITE', '1')
endif
if cc.has_header_symbol('sys/mount.h', 'BLKSSZGET')
  conf.set('HAVE_BLKSSZGET', '1')
endif

if host_machine.system() == 'freebsd'
  if cc.has_type('struct efi_esrt_entry_v1', prefix: '#include <sys/types.h>\n#include <sys/efiio.h>')
    conf.set('HAVE_FREEBSD_ESRT', '1')
  endif
endif

launchctl = find_program('launchctl',
  required: get_option('launchd').disable_auto_if(host_machine.system() != 'darwin').allowed(),
)

# this is way less hassle than including TargetConditionals.h and looking for TARGET_OS_MAC=1
if host_machine.system() == 'darwin'
  conf.set('HOST_MACHINE_SYSTEM_DARWIN', '1')
  summary({
    'launchd': launchctl,
    'launchd_agent_dir': get_option('launchd_agent_dir'),
  }, section: 'Darwin options')
endif

# EFI
if build_standalone
  efi_app_location = join_paths(libexecdir, 'fwupd', 'efi')
  conf.set_quoted('EFI_APP_LOCATION', efi_app_location)
  if host_cpu == 'x86'
    EFI_MACHINE_TYPE_NAME = 'ia32'
  elif host_cpu == 'x86_64'
    EFI_MACHINE_TYPE_NAME = 'x64'
  elif host_cpu == 'arm'
    EFI_MACHINE_TYPE_NAME = 'arm'
  elif host_cpu == 'aarch64'
    EFI_MACHINE_TYPE_NAME = 'aa64'
  elif host_cpu == 'loongarch64'
    EFI_MACHINE_TYPE_NAME = 'loongarch64'
  elif host_cpu == 'riscv64'
    EFI_MACHINE_TYPE_NAME = 'riscv64'
  else
    EFI_MACHINE_TYPE_NAME = ''
  endif
  conf.set_quoted('EFI_MACHINE_TYPE_NAME', EFI_MACHINE_TYPE_NAME)
endif

flashrom = get_option('plugin_flashrom').disable_auto_if(host_machine.system() != 'linux')
allow_flashrom = flashrom.allowed()
if build_standalone
  libflashrom = dependency('flashrom',
                            fallback: ['flashrom', 'flashrom_dep'],
                            required: flashrom)
endif

if libsystemd.found()
  systemd = dependency('systemd', version: '>= 249', required: get_option('systemd'))
  conf.set('HAVE_SYSTEMD' , '1')
  conf.set('HAVE_LOGIND' , '1')
  systemd_root_prefix = get_option('systemd_root_prefix')
  if systemd_root_prefix == ''
    systemdunitdir = systemd.get_variable(pkgconfig: 'systemdsystemunitdir')
    systemd_shutdown_dir = systemd.get_variable(pkgconfig: 'systemdshutdowndir')
    systemd_modules_load_dir = systemd.get_variable(pkgconfig: 'modulesloaddir')
    systemd_sysusers_dir = systemd.get_variable(pkgconfig: 'sysusersdir')
  else
    systemdunitdir = systemd.get_variable(pkgconfig: 'systemdsystemunitdir', pkgconfig_define: ['rootprefix', systemd_root_prefix])
    systemd_shutdown_dir = systemd.get_variable(pkgconfig: 'systemdshutdowndir', pkgconfig_define: ['root_prefix', systemd_root_prefix])
    systemd_modules_load_dir = systemd.get_variable(pkgconfig: 'modulesloaddir', pkgconfig_define: ['root_prefix', systemd_root_prefix])
    systemd_sysusers_dir = systemd.get_variable(pkgconfig: 'sysusersdir', pkgconfig_define: ['root_prefix', systemd_root_prefix])
  endif
endif

elogind = dependency('systemd', 'libelogind', required: get_option('elogind'))
if elogind.found()
  conf.set('HAVE_LOGIND' , '1')
endif

consolekit = get_option('consolekit').disable_auto_if(host_machine.system() != 'linux').allowed()
if consolekit
  conf.set('HAVE_CONSOLEKIT' , '1')
endif

supported_build = get_option('supported_build').disable_auto_if(not tag).allowed()
if supported_build
    conf.set('SUPPORTED_BUILD', '1')
endif

# compatibility with behavior in < kernel 6.3
if get_option('thinklmi_compat')
  conf.set('FU_THINKLMI_COMPAT', '1')
endif

gnome = import('gnome')
i18n = import('i18n')

conf.set_quoted('FWUPD_PREFIX', prefix)
conf.set_quoted('FWUPD_BINDIR', bindir)
conf.set_quoted('FWUPD_LIBDIR', libdir)
conf.set_quoted('FWUPD_LIBEXECDIR', libexecdir)
conf.set_quoted('FWUPD_DATADIR', datadir)
conf.set_quoted('FWUPD_LOCALSTATEDIR', localstatedir)
conf.set_quoted('FWUPD_SYSCONFDIR', sysconfdir)
conf.set_quoted('FWUPD_LOCALEDIR', localedir)

if build_standalone
if host_machine.system() == 'windows'
  libdir_pkg = bindir
else
  libdir_pkg = join_paths(libdir, 'fwupd-@0@'.format(fwupd_version))
endif
conf.set_quoted('FWUPD_LIBDIR_PKG', libdir_pkg)
endif

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())

if get_option('dbus_socket_address') != ''
  conf.set_quoted('FWUPD_DBUS_SOCKET_ADDRESS', get_option('dbus_socket_address'))
endif

motd_file = '85-fwupd'
motd_dir = 'motd.d'
conf.set_quoted('MOTD_FILE', motd_file)
conf.set_quoted('MOTD_DIR', motd_dir)

conf.set_quoted('FU_DEFAULT_P2P_POLICY', get_option('p2p_policy'))

configure_file(
  output: 'config.h',
  configuration: conf
)

libdrm_amdgpu = dependency('libdrm_amdgpu',
                           version: '>= 2.4.113',
                           required: get_option('plugin_amdgpu'))
protobufc = dependency('libprotobuf-c', required: get_option('plugin_logitech_bulkcontroller'))
protoc = find_program('protoc', 'protoc-c', required: get_option('plugin_logitech_bulkcontroller'))

root_incdir = include_directories('.')

fwupd_gir = []
gir_dep = dependency('gobject-introspection-1.0', required: get_option('introspection'))
introspection = get_option('introspection').disable_auto_if(host_machine.system() != 'linux').disable_auto_if(not gir_dep.found())

gidocgen_dep = dependency('gi-docgen',
  version: '>= 2021.1',
  native: true,
  fallback: ['gi-docgen', 'dummy_dep'],
  required: get_option('docs'),
)
gidocgen_app = find_program('gi-docgen', required: gidocgen_dep.found())
build_docs = gidocgen_dep.found() and gidocgen_app.found() and introspection.allowed()

if build_docs and gidocgen_dep.version().version_compare('< 2022.2')
  markdown_version = run_command(
    [python3, '-c', 'import markdown; print(markdown.__version__)'],
    check: true,
  ).stdout().strip()
  build_docs = get_option('docs').require(
    markdown_version.version_compare('>=3.2'),
    error_message: 'docs=enabled requires at least markdown >= 3.2'
  ).allowed()
endif

jinja2 = run_command(
  [python3, '-c', 'import jinja2; print(jinja2.__version__)'],
  check: true,
)
if jinja2.stderr().strip() != ''
  error('Python module jinja2 not found')
endif

# using "meson configure -Db_sanitize=address,undefined" is super useful in finding corruption,
# but it does not work with our GMainContext-abuse tests...
if get_option('b_sanitize') in ['address,undefined', 'address', 'undefined', 'leak']
  run_sanitize_unsafe_tests = false
else
  run_sanitize_unsafe_tests = true
endif

# take foo.rs and generate foo-struct.c and foo-struct.h files like protobuf_c
rustgen = generator(python3,
  output  : ['@BASENAME@-struct.c', '@BASENAME@-struct.h'],
  arguments : [
    join_paths(meson.project_source_root(), 'libfwupdplugin', 'rustgen.py'),
    '@INPUT@',
    '@OUTPUT0@',
    '@OUTPUT1@',
  ],
)

dbusmock = run_command(
  [python3, '-c', 'import dbusmock; print(dbusmock.__version__)'],
  check: false,
)

umockdev_integration_tests = get_option('umockdev_tests')                     \
                              .disable_auto_if(not get_option('tests'))       \
                              .disable_auto_if(not introspection.allowed())   \
                              .disable_auto_if(not run_sanitize_unsafe_tests) \
                              .disable_auto_if(dbusmock.returncode() != 0)
umockdev = dependency('umockdev-1.0', required: get_option('umockdev_tests'))

if dbusmock.returncode() != 0 and get_option('umockdev_tests').allowed()
  warning('python dbusmock not found, umockdev tests will be disabled')
endif

allow_uefi_capsule = get_option('plugin_uefi_capsule').disable_auto_if(host_machine.system() not in ['linux', 'freebsd']).allowed()

subdir('generate-build')
subdir('libfwupd')
if polkit.found()
  subdir('policy')
endif
if build_standalone
  man_md = []
  md_targets = []
  plugin_quirks = []
  subdir('libfwupdplugin')
  subdir('data')
  subdir('po')
  subdir('contrib')

  # common to all plugins
  plugin_builtins = []
  plugin_incdirs = [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ]
  plugin_libs = [
    fwupd,
    fwupdplugin,
  ]
  subdir('plugins')
  subdir('src')
  subdir('docs')

  # append all the quirks into one big file and gzip it
  custom_target('builtin-quirk-gz',
    input: plugin_quirks,
    output: 'builtin.quirk.gz',
    command: [
      generate_quirk_builtin,
      '@OUTPUT@',
      '@INPUT@',
    ],
    install: true,
    install_dir: join_paths(datadir, 'fwupd', 'quirks.d'),
  )
endif

if build_daemon and libsystemd.found() and offline
  install_symlink('fwupd-offline-update.service',
    install_dir: join_paths(systemdunitdir, 'system-update.target.wants'),
    pointing_to: join_paths('..', 'fwupd-offline-update.service')
  )
endif

if libsystemd.found()
  summary({
    'systemd_unit_user': get_option('systemd_unit_user'),
    'systemd unit dir': systemdunitdir,
    'systemd shutdown dir': systemd_shutdown_dir,
    'systemd modules dir': systemd_modules_load_dir,
    'systemd sysusers dir': systemd_sysusers_dir,
  }, section: 'systemd options')
endif

summary({
  'fwupdtool': build_standalone,
  'fwupd (daemon)': build_daemon
}, section: 'build targets')

summary({
  'cbor': cbor,
  'consolekit': consolekit,
  'dbus_socket_address': get_option('dbus_socket_address'),
  'vendor_ids_dir': vendor_ids_dir,
  'docs': build_docs,
  'elogind': elogind,
  'gnutls': gnutls,
  'introspection': introspection.allowed(),
  'libcurl': libcurl,
  'lzma': lzma,
  'polkit': polkit,
  'python3': python3,
  'supported_build': supported_build,
  'static_analysis': static_analysis,
  'tests': get_option('tests'),
  'umockdev_tests': umockdev_integration_tests.allowed(),
}, section: 'project features')

if build_daemon
  summary({
    'bluez': bluez.allowed(),
    'libusb': libusb,
    'gudev': gudev,
    'hsi': hsi,
    'lvfs_metadata_format': lvfs_metadata_format,
    'libarchive': libarchive.found(),
    'offline': offline,
    'passim': passim,
    'sqlite': sqlite,
    'thinklmi_compat': get_option('thinklmi_compat'),
    'GPG support': supported_gpg,
    'PKCS7 support': supported_pkcs7,
  }, section: 'daemon features')
  en=[]
  dis=[]
  foreach plugin: plugins.keys()
    if plugins[plugin]
      en += plugin
    else
      dis += plugin
    endif
  endforeach
  summary({
    'enabled': ', '.join(en),
    'disabled': ', '.join(dis),
  }, section: 'plugins')
endif
