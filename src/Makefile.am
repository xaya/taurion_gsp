.PHONY: build-tests

noinst_LTLIBRARIES = libtaurion.la
bin_PROGRAMS = tauriond
dist_noinst_SCRIPTS = update-version.sh

EXTRA_DIST = \
  rpc-stubs/nonstate.json \
  rpc-stubs/pxd.json

RPC_STUBS = \
  rpc-stubs/nonstaterpcserverstub.h \
  rpc-stubs/pxrpcserverstub.h
BUILT_SOURCES = $(RPC_STUBS) version.cpp
CLEANFILES = $(RPC_STUBS)

.PHONY: getversion

libtaurion_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(XAYAGAME_CFLAGS) $(JSON_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS) $(PROTOBUF_CFLAGS)
libtaurion_la_LIBADD = \
  $(top_builddir)/database/libdatabase.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(top_builddir)/proto/libpxproto.la \
  $(XAYAGAME_LIBS) $(JSON_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS) $(PROTOBUF_LIBS)
libtaurion_la_SOURCES = \
  buildings.cpp \
  burnsale.cpp \
  combat.cpp \
  context.cpp \
  dynobstacles.cpp \
  fame.cpp \
  fitments.cpp \
  forks.cpp \
  gamestatejson.cpp \
  jsonutils.cpp \
  logic.cpp \
  mining.cpp \
  modifier.cpp \
  movement.cpp \
  moveprocessor.cpp \
  ongoings.cpp \
  params.cpp \
  pending.cpp \
  prospecting.cpp \
  protoutils.cpp \
  resourcedist.cpp \
  services.cpp \
  spawn.cpp \
  trading.cpp
libtaurionheaders = \
  buildings.hpp \
  burnsale.hpp \
  combat.hpp \
  context.hpp \
  dynobstacles.hpp dynobstacles.tpp \
  fame.hpp \
  fitments.hpp \
  forks.hpp \
  gamestatejson.hpp \
  jsonutils.hpp \
  logic.hpp \
  mining.hpp \
  modifier.hpp \
  movement.hpp movement.tpp \
  moveprocessor.hpp \
  ongoings.hpp \
  params.hpp \
  pending.hpp \
  prospecting.hpp \
  protoutils.hpp \
  resourcedist.hpp \
  services.hpp \
  spawn.hpp \
  trading.hpp

tauriond_CXXFLAGS = \
  -I$(top_srcdir) \
  $(XAYAGAME_CFLAGS) $(CHARON_CFLAGS) \
  $(JSON_CFLAGS) $(MHD_CLFGAS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS) $(PROTOBUF_CFLAGS)
tauriond_LDADD = \
  $(builddir)/libtaurion.la \
  $(top_builddir)/mapdata/libmapdata.la \
  $(top_builddir)/database/libdatabase.la \
  $(XAYAGAME_LIBS) $(CHARON_LIBS) \
  $(JSON_LIBS) $(MHD_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS) $(PROTOBUF_LIBS)
tauriond_SOURCES = main.cpp \
  pxrpcserver.cpp \
  rest.cpp \
  version.cpp
tauriondheaders = \
  pxrpcserver.hpp \
  rest.hpp \
  version.hpp \
  \
  rpc-stubs/nonstaterpcserverstub.h \
  rpc-stubs/pxrpcserverstub.h

noinst_HEADERS = $(libtaurionheaders) $(tauriondheaders)

check_LTLIBRARIES = libtestutils.la
check_PROGRAMS = tests benchmarks
TESTS = tests benchmarks

build-tests: $(check_PROGRAMS)

libtestutils_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(XAYAGAME_CFLAGS) $(JSON_CFLAGS)
libtestutils_la_LIBADD = \
  $(builddir)/libtaurion.la \
  $(top_builddir)/mapdata/libmapdata.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(XAYAGAME_LIBS) $(JSON_LIBS)
libtestutils_la_SOURCES = \
  testutils.cpp

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GTEST_MAIN_CFLAGS) \
  $(JSON_CFLAGS) $(GTEST_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS) $(PROTOBUF_CFLAGS)
tests_LDADD = \
  $(builddir)/libtaurion.la \
  $(builddir)/libtestutils.la \
  $(top_builddir)/database/libdbtest.la \
  $(top_builddir)/database/libdatabase.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(top_builddir)/mapdata/libmapdata.la \
  $(top_builddir)/proto/libpxproto.la \
  $(GTEST_MAIN_LIBS) \
  $(JSON_LIBS) $(GTEST_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS) $(PROTOBUF_LIBS)
tests_SOURCES = \
  buildings_tests.cpp \
  burnsale_tests.cpp \
  combat_tests.cpp \
  dynobstacles_tests.cpp \
  fame_tests.cpp \
  fitments_tests.cpp \
  forks_tests.cpp \
  gamestatejson_tests.cpp \
  jsonutils_tests.cpp \
  logic_tests.cpp \
  mining_tests.cpp \
  modifier_tests.cpp \
  movement_tests.cpp \
  moveprocessor_tests.cpp \
  ongoings_tests.cpp \
  pending_tests.cpp \
  prospecting_tests.cpp \
  protoutils_tests.cpp \
  resourcedist_tests.cpp \
  services_tests.cpp \
  spawn_tests.cpp \
  testutils_tests.cpp \
  trading_tests.cpp
check_HEADERS = \
  fame_tests.hpp \
  \
  testutils.hpp

benchmarks_CXXFLAGS = \
  -I$(top_srcdir) \
  $(JSON_CFLAGS) $(GLOG_CFLAGS) $(PROTOBUF_CFLAGS) $(BENCHMARK_CFLAGS)
benchmarks_LDADD = \
  $(builddir)/libtaurion.la \
  $(builddir)/libtestutils.la \
  $(top_builddir)/database/libdbtest.la \
  $(top_builddir)/database/libdatabase.la \
  $(top_builddir)/hexagonal/libbenchmain.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(top_builddir)/mapdata/libmapdata.la \
  $(top_builddir)/proto/libpxproto.la \
  $(JSON_LIBS) $(GLOG_LIBS) $(PROTOBUF_LIBS) $(BENCHMARK_LIBS)
benchmarks_SOURCES = \
  combat_damage_bench.cpp \
  combat_target_bench.cpp \
  movement_bench.cpp

rpc-stubs/nonstaterpcserverstub.h: $(srcdir)/rpc-stubs/nonstate.json
	jsonrpcstub "$<" \
          --cpp-server=NonStateRpcServerStub --cpp-server-file="$@"
rpc-stubs/pxrpcserverstub.h: $(srcdir)/rpc-stubs/pxd.json
	jsonrpcstub "$<" --cpp-server=PXRpcServerStub --cpp-server-file="$@"

version.cpp: getversion
getversion:
	$(srcdir)/update-version.sh \
          $(builddir)/version.cpp \
          $(srcdir)/version.cpp
