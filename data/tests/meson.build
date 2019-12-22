# generate private PKCS7 key
certtool = find_program('certtool')
pkcs7_privkey = custom_target('test-privkey.pem',
  output: 'test-privkey.pem',
  command: [certtool, '--generate-privkey', '--outfile', '@OUTPUT@'],
)

subdir('builder')
subdir('pki')
subdir('colorhug')
subdir('missing-hwid')
subdir('multiple-rels')
