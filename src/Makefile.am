# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstnxvideoenc.la

##############################################################################
# TODO: for the next set of variables, name the prefix if you named the .la, #
#  e.g. libmysomething.la => libmysomething_la_SOURCES                       #
#                            libmysomething_la_CFLAGS                        #
#                            libmysomething_la_LIBADD                        #
#                            libmysomething_la_LDFLAGS                       #
##############################################################################

# sources used to compile this plug-in
libgstnxvideoenc_la_SOURCES = gstnxvideoenc.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstnxvideoenc_la_CFLAGS =\
	$(GST_CFLAGS)			\
	-I${includedir} \
	-I${includedir}/libdrm

libgstnxvideoenc_la_LIBADD = \
	$(GST_LIBS)			\
	-lgstvideo-1.0		\
	-lgstpbutils-1.0	\
	-lnxgstmeta			\
	-ldrm				\
	-lnx_video_api

libgstnxvideoenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -L$(libdir)
libgstnxvideoenc_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstnxvideoenc.h
