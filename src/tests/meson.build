# generate private PKCS7 key
certtool = find_program(['gnutls-certtool', 'certtool'], required : false)

if certtool.found()
  pkcs7_privkey = custom_target('test-privkey.pem',
    output: 'test-privkey.pem',
    command: [certtool, '--generate-privkey', '--outfile', '@OUTPUT@'],
  )
endif

subdir('auth')
subdir('pki')
subdir('colorhug')
subdir('missing-hwid')
subdir('multiple-rels')
subdir('host-emulate')
