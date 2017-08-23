# Generating the p7b file manually:

    certtool --p7-detached-sign --p7-time \
        --load-privkey LVFS/pkcs7/secure-lvfs.rhcloud.com.key \
        --load-certificate LVFS/pkcs7/secure-lvfs.rhcloud.com_signed.pem \
        --infile firmware.bin \
        --outfile firmware.bin.p7b
