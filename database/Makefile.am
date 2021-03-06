noinst_LTLIBRARIES = libdatabase.la

EXTRA_DIST = schema.sql schema_head.cpp schema_tail.cpp
CLEANFILES = schema.cpp

libdatabase_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(XAYAGAME_CFLAGS) \
  $(GLOG_CFLAGS) $(SQLITE3_CFLAGS) $(GMP_CFLAGS) $(PROTOBUF_CFLAGS)
libdatabase_la_LIBADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(top_builddir)/proto/libpxproto.la \
  $(XAYAGAME_LIBS) \
  $(GLOG_LIBS) $(SQLITE3_LIBS) $(GMP_LIBS) $(PROTOBUF_LIBS)
libdatabase_la_SOURCES = \
  account.cpp \
  building.cpp \
  character.cpp \
  combat.cpp \
  coord.cpp \
  damagelists.cpp \
  database.cpp \
  dex.cpp \
  faction.cpp \
  fighter.cpp \
  inventory.cpp \
  itemcounts.cpp \
  moneysupply.cpp \
  ongoing.cpp \
  region.cpp \
  schema.cpp \
  target.cpp \
  uniquehandles.cpp
noinst_HEADERS = \
  amount.hpp \
  account.hpp \
  building.hpp \
  character.hpp \
  combat.hpp combat.tpp \
  coord.hpp coord.tpp \
  damagelists.hpp \
  database.hpp database.tpp \
  dex.hpp \
  faction.hpp faction.tpp \
  fighter.hpp \
  inventory.hpp \
  itemcounts.hpp \
  moneysupply.hpp \
  ongoing.hpp \
  lazyproto.hpp lazyproto.tpp \
  region.hpp \
  schema.hpp \
  target.hpp \
  uniquehandles.hpp uniquehandles.tpp

check_LTLIBRARIES = libdbtest.la
check_PROGRAMS = tests benchmarks
TESTS = tests benchmarks

libdbtest_la_CXXFLAGS = \
  $(XAYAGAME_CFLAGS) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS) $(SQLITE3_CFLAGS) $(BENCHMARK_CFLAGS)
libdbtest_la_LIBADD = \
  $(builddir)/libdatabase.la \
  $(XAYAGAME_LIBS) \
  $(GTEST_LIBS) $(GLOG_LIBS) $(SQLITE3_LIBS) $(BENCHMARK_LIBS)
libdbtest_la_SOURCES = \
  dbtest.cpp
check_HEADERS = \
  dbtest.hpp

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(XAYAGAME_CFLAGS) \
  $(GTEST_MAIN_CFLAGS) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS)
tests_LDADD = \
  $(builddir)/libdbtest.la \
  $(builddir)/libdatabase.la \
  $(top_builddir)/proto/libpxproto.la \
  $(XAYAGAME_LIBS) \
  $(GTEST_MAIN_LIBS) \
  $(GTEST_LIBS) $(GLOG_LIBS)
tests_SOURCES = \
  account_tests.cpp \
  building_tests.cpp \
  character_tests.cpp \
  combat_tests.cpp \
  coord_tests.cpp \
  damagelists_tests.cpp \
  database_tests.cpp \
  dex_tests.cpp \
  faction_tests.cpp \
  fighter_tests.cpp \
  inventory_tests.cpp \
  itemcounts_tests.cpp \
  lazyproto_tests.cpp \
  moneysupply_tests.cpp \
  ongoing_tests.cpp \
  region_tests.cpp \
  schema_tests.cpp \
  target_tests.cpp \
  uniquehandles_tests.cpp

benchmarks_CXXFLAGS = \
  -I$(top_srcdir) \
  $(JSON_CFLAGS) $(GLOG_CFLAGS) $(PROTOBUF_CFLAGS) $(BENCHMARK_CFLAGS)
benchmarks_LDADD = \
  $(builddir)/libdbtest.la \
  $(builddir)/libdatabase.la \
  $(top_builddir)/hexagonal/libbenchmain.la \
  $(top_builddir)/proto/libpxproto.la \
  $(JSON_LIBS) $(GLOG_LIBS) $(PROTOBUF_LIBS) $(BENCHMARK_LIBS)
benchmarks_SOURCES = \
  character_bench.cpp \
  target_bench.cpp

schema.cpp: schema_head.cpp schema.sql schema_tail.cpp
	cat $^ >$@
