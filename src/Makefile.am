noinst_LTLIBRARIES = libpx.la
bin_PROGRAMS = pxd

EXTRA_DIST = rpc-stubs/pxd.json

RPC_STUBS = \
  rpc-stubs/pxrpcserverstub.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

libpx_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(XAYAGAME_CFLAGS) $(JSON_CFLAGS) $(GLOG_CFLAGS) $(PROTOBUF_CFLAGS)
libpx_la_LIBADD = \
  $(top_builddir)/database/libdatabase.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(top_builddir)/proto/libpxproto.la \
  $(XAYAGAME_LIBS) $(JSON_LIBS) $(GLOG_LIBS) $(PROTOBUF_LIBS)
libpx_la_SOURCES = \
  combat.cpp \
  gamestatejson.cpp \
  jsonutils.cpp \
  logic.cpp \
  movement.cpp \
  moveprocessor.cpp \
  params.cpp \
  protoutils.cpp
libpxheaders = \
  amount.hpp \
  combat.hpp \
  gamestatejson.hpp \
  jsonutils.hpp \
  logic.hpp \
  movement.hpp \
  moveprocessor.hpp \
  params.hpp \
  protoutils.hpp

pxd_CXXFLAGS = \
  -I$(top_srcdir) \
  $(XAYAGAME_CFLAGS) \
  $(JSON_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS) $(PROTOBUF_CFLAGS)
pxd_LDADD = \
  $(builddir)/libpx.la \
  $(top_builddir)/mapdata/libmapdata.la \
  $(top_builddir)/database/libdatabase.la \
  $(XAYAGAME_LIBS) \
  $(JSON_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS) $(PROTOBUF_LIBS) \
  -lstdc++fs
pxd_SOURCES = main.cpp \
  pxrpcserver.cpp
pxdheaders = \
  pxrpcserver.hpp \
  \
  rpc-stubs/pxrpcserverstub.h

noinst_HEADERS = $(libpxheaders) $(pxdheaders)

check_PROGRAMS = tests benchmarks
TESTS = tests benchmarks

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GTEST_MAIN_CFLAGS) \
  $(JSON_CFLAGS) $(GTEST_CFLAGS) $(GLOG_CFLAGS) $(PROTOBUF_CFLAGS)
tests_LDADD = \
  $(builddir)/libpx.la \
  $(top_builddir)/database/libdbtest.la \
  $(top_builddir)/database/libdatabase.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(top_builddir)/mapdata/libmapdata.la \
  $(top_builddir)/proto/libpxproto.la \
  $(GTEST_MAIN_LIBS) \
  $(JSON_LIBS) $(GTEST_LIBS) $(GLOG_LIBS) $(PROTOBUF_LIBS)
tests_SOURCES = \
  combat_tests.cpp \
  gamestatejson_tests.cpp \
  jsonutils_tests.cpp \
  logic_tests.cpp \
  movement_tests.cpp \
  moveprocessor_tests.cpp \
  protoutils_tests.cpp

benchmarks_CXXFLAGS = \
  -I$(top_srcdir) \
  $(JSON_CFLAGS) $(GLOG_CFLAGS) $(PROTOBUF_CFLAGS) $(BENCHMARK_CFLAGS)
benchmarks_LDADD = \
  $(builddir)/libpx.la \
  $(top_builddir)/database/libdbtest.la \
  $(top_builddir)/database/libdatabase.la \
  $(top_builddir)/hexagonal/libbenchmain.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(top_builddir)/proto/libpxproto.la \
  $(JSON_LIBS) $(GLOG_LIBS) $(PROTOBUF_LIBS) $(BENCHMARK_LIBS)
benchmarks_SOURCES = \
  combat_bench.cpp \
  movement_bench.cpp

rpc-stubs/pxrpcserverstub.h: $(srcdir)/rpc-stubs/pxd.json
	jsonrpcstub "$<" --cpp-server=PXRpcServerStub --cpp-server-file="$@"
