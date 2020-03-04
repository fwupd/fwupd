efi_cc = get_option('efi-cc')
efi_ld = get_option('efi-ld')
efi_ldsdir = get_option('efi-ldsdir')
efi_incdir = get_option('efi-includedir')

gnu_efi_path_arch = ''
foreach name : [gnu_efi_arch, EFI_MACHINE_TYPE_NAME]
  if (gnu_efi_path_arch == '' and name != '' and
    cc.has_header('@0@/@1@/efibind.h'.format(efi_incdir, name)))
    gnu_efi_path_arch = name
  endif
endforeach

if gnu_efi_path_arch != '' and EFI_MACHINE_TYPE_NAME == ''
  error('gnu-efi is available, but EFI_MACHINE_TYPE_NAME is unknown')
endif

efi_libdir = get_option('efi-libdir')
if efi_libdir == ''
  cmd = 'cd /usr/lib/$(@0@ -print-multi-os-directory) && pwd'.format(efi_cc)
  ret = run_command('sh', '-c', cmd)
  if ret.returncode() == 0
    efi_libdir = ret.stdout().strip()
  endif
endif

have_gnu_efi = gnu_efi_path_arch != '' and efi_libdir != ''

if not have_gnu_efi
  error('gnu-efi support requested, but headers were not found')
endif

arch_lds = 'efi.lds'
arch_crt = 'crt0.o'
if efi_ldsdir == ''
  efi_ldsdir = join_paths(efi_libdir, 'gnuefi', gnu_efi_path_arch)
  cmd = run_command('test', '-f', join_paths(efi_ldsdir, arch_lds))
  if cmd.returncode() != 0
    arch_lds = 'elf_@0@_efi.lds'.format(gnu_efi_path_arch)
    arch_crt = 'crt0-efi-@0@.o'.format(gnu_efi_path_arch)
    efi_ldsdir = join_paths(efi_libdir, 'gnuefi')
    cmd = run_command('test', '-f', join_paths(efi_ldsdir, arch_lds))
  endif
  if cmd.returncode() != 0
    efi_ldsdir = efi_libdir
    cmd = run_command('test', '-f', join_paths(efi_ldsdir, arch_lds))
    if cmd.returncode() != 0
      error('Cannot find @0@'.format(arch_lds))
    endif
  endif
else
  cmd = run_command('test', '-f', join_paths(efi_ldsdir, arch_lds))
  if cmd.returncode() != 0
    arch_lds = 'elf_@0@_efi.lds'.format(gnu_efi_path_arch)
    arch_crt = 'crt0-efi-@0@.o'.format(gnu_efi_path_arch)
    cmd = run_command('test', '-f', join_paths(efi_ldsdir, arch_lds))
  endif
  if cmd.returncode() != 0
    error('Cannot find @0@'.format(arch_lds))
  endif
endif

message('efi-libdir: "@0@"'.format(efi_libdir))
message('efi-ldsdir: "@0@"'.format(efi_ldsdir))
message('efi-includedir: "@0@"'.format(efi_incdir))

debugdir = join_paths (libdir, 'debug')
compile_args = ['-Og',
                '-g3',
                '--param=ssp-buffer-size=4',
                '-fexceptions',
                '-Wall',
                '-Wextra',
                '-Wvla',
                '-std=gnu11',
                '-fpic',
                '-fshort-wchar',
                '-ffreestanding',
                '-fno-strict-aliasing',
                '-fno-stack-protector',
                '-fno-stack-check',
                '-fno-merge-constants',
                '-Wsign-compare',
                '-Wno-missing-field-initializers',
                '-Wno-address-of-packed-member',
                '-grecord-gcc-switches',
                '-DDEBUGDIR="@0@"'.format(debugdir),
                '-isystem', efi_incdir,
                '-isystem', join_paths(efi_incdir, gnu_efi_path_arch)]
if get_option('werror')
    compile_args += '-Werror'
endif
if efi_arch == 'x86_64'
  compile_args += ['-mno-red-zone',
                   '-mno-sse',
                   '-mno-mmx',
                   '-DEFI_FUNCTION_WRAPPER',
                   '-DGNU_EFI_USE_MS_ABI']
elif efi_arch == 'ia32'
  compile_args += ['-mno-sse',
                   '-mno-mmx',
                   '-mno-red-zone',
                   '-m32']
# no special cases for aarch64 or arm
endif

efi_ldflags = ['-T',
               join_paths(efi_ldsdir, arch_lds),
               '-shared',
               '-Bsymbolic',
               '-nostdlib',
               '-znocombreloc',
               '-L', efi_ldsdir,
               '-L', efi_libdir,
               join_paths(efi_ldsdir, arch_crt)]
if efi_arch == 'aarch64' or efi_arch == 'arm'
  # Aarch64 and ARM32 don't have an EFI capable objcopy. Use 'binary'
  # instead, and add required symbols manually.
  efi_ldflags += ['--defsym=EFI_SUBSYSTEM=0xa']
  efi_format = ['-O', 'binary']
else
  efi_format = ['--target=efi-app-@0@'.format(gnu_efi_arch)]
endif

libgcc_file_name = run_command(efi_cc, '-print-libgcc-file-name').stdout().strip()
efi_name = 'fwupd@0@.efi'.format(EFI_MACHINE_TYPE_NAME)

o_file1 = custom_target('fwupdate.o',
                        input : 'fwupdate.c',
                        output : 'fwupdate.o',
                        command : [efi_cc, '-c', '@INPUT@', '-o', '@OUTPUT@']
                                  + compile_args)
o_file2 = custom_target('fwup-debug.o',
                        input : 'fwup-debug.c',
                        output : 'fwup-debug.o',
                        command : [efi_cc, '-c', '@INPUT@', '-o', '@OUTPUT@']
                                  + compile_args)
o_file3 = custom_target('fwup-efi.o',
                        input : 'fwup-efi.c',
                        output : 'fwup-efi.o',
                        command : [efi_cc, '-c', '@INPUT@', '-o', '@OUTPUT@']
                                  + compile_args)
o_file4 = custom_target('fwup-common.o',
                        input : 'fwup-common.c',
                        output : 'fwup-common.o',
                        command : [efi_cc, '-c', '@INPUT@', '-o', '@OUTPUT@']
                                  + compile_args)

so = custom_target('fwup.so',
                   input : [o_file1, o_file2, o_file3, o_file4],
                   output : 'fwup.so',
                   command : [efi_ld, '-o', '@OUTPUT@'] +
                             efi_ldflags + ['@INPUT@'] +
                             ['-lefi', '-lgnuefi', libgcc_file_name])

build_tool = join_paths(meson.source_root(), 'plugins', 'uefi', 'efi', 'generate_binary.sh')
app = custom_target(efi_name,
                    input : so,
                    output : efi_name,
                    command : [build_tool, '@INPUT@', '@OUTPUT@', efi_format],
                    install : true,
                    install_dir : efi_app_location)

dbg = custom_target('efi_debug',
                    input : so,
                    output : efi_name + '.debug',
                    command : [objcopy,
                               '-j', '.text',
                               '-j', '.sdata',
                               '-j', '.data',
                               '-j', '.dynamic',
                               '-j', '.dynsym',
                               '-j', '.rel*',
                               '-j', '.rela*',
                               '-j', '.reloc',
                               '-j', '.eh_frame',
                               '-j', '.debug*',
                               '-j', '.note.gnu.build-id']
                               + efi_format +
                               ['@INPUT@', '@OUTPUT@'],
                    install : false,
                    install_dir : debugdir)
