#!/bin/sh
# you can either set the environment variables AUTOCONF, AUTOHEADER, AUTOMAKE,
# ACLOCAL, AUTOPOINT and/or LIBTOOLIZE to the right versions, or leave them
# unset and get the defaults

autoreconf --verbose --force --install --make || {
 echo 'autogen.sh failed';
 exit 1;
}

# single compile example
# #> source /opt/poky/2.5.1/environment-setup-cortexa9hf-neon-poky-linux-gnueabi
# #> ./configure --host=arm-poky-linux-gnueabi --prefix=$(SDKTARGETSYSROOT)/usr
# #> make