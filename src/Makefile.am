noinst_LTLIBRARIES = libpx.la
bin_PROGRAMS = pxd

EXTRA_DIST = rpc-stubs/pxd.json

RPC_STUBS = \
  rpc-stubs/pxrpcserverstub.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

libpx_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(JSON_CFLAGS) $(GLOG_CFLAGS)
libpx_la_LIBADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(JSON_LIBS) $(GLOG_LIBS)
libpx_la_SOURCES = \
  basemap.cpp
libpxheaders = \
  basemap.hpp

pxd_CXXFLAGS = \
  $(XAYAGAME_CFLAGS) $(JSON_CFLAGS) $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
pxd_LDADD = \
  $(builddir)/libpx.la \
  $(XAYAGAME_LIBS) $(JSON_LIBS) $(GLOG_LIBS) $(GFLAGS_LIBS) \
  -lstdc++fs
pxd_SOURCES = main.cpp \
  logic.cpp \
  pxrpcserver.cpp
pxdheaders = \
  logic.hpp \
  pxrpcserver.hpp \
  \
  rpc-stubs/pxrpcserverstub.h

noinst_HEADERS = $(libpxheaders) $(pxdheaders)

rpc-stubs/pxrpcserverstub.h: $(srcdir)/rpc-stubs/pxd.json
	jsonrpcstub "$<" --cpp-server=PXRpcServerStub --cpp-server-file="$@"
