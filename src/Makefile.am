bin_PROGRAMS = pxd

EXTRA_DIST = rpc-stubs/pxd.json

RPC_STUBS = \
  rpc-stubs/pxrpcserverstub.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

pxd_CXXFLAGS = \
  $(XAYAGAME_CFLAGS) $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
pxd_LDADD = \
  $(XAYAGAME_LIBS) $(GLOG_LIBS) $(GFLAGS_LIBS) \
  -lstdc++fs
pxd_SOURCES = main.cpp \
  basemap.cpp \
  logic.cpp \
  pxrpcserver.cpp
noinst_HEADERS = \
  basemap.hpp \
  logic.hpp \
  pxrpcserver.hpp \
  \
  rpc-stubs/pxrpcserverstub.h

rpc-stubs/pxrpcserverstub.h: $(srcdir)/rpc-stubs/pxd.json
	jsonrpcstub "$<" --cpp-server=PXRpcServerStub --cpp-server-file="$@"
