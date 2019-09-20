noinst_LTLIBRARIES = libhexagonal.la

libhexagonal_la_CXXFLAGS = $(GLOG_CFLAGS)
libhexagonal_la_LIBADD = $(GLOG_LIBS)
libhexagonal_la_SOURCES = \
  pathfinder.cpp \
  ring.cpp
noinst_HEADERS = \
  coord.hpp coord.tpp \
  pathfinder.hpp pathfinder.tpp \
  rangemap.hpp rangemap.tpp \
  ring.hpp

check_LTLIBRARIES = libbenchmain.la
check_PROGRAMS = tests benchmarks
TESTS = tests benchmarks

libbenchmain_la_CXXFLAGS = $(BENCHMARK_CFLAGS) $(GLOG_CFLAGS)
libbenchmain_la_LIBADD = $(BENCHMARK_LIBS) $(GLOG_LIBS)
libbenchmain_la_SOURCES = benchmain.cpp

tests_CXXFLAGS = \
  $(GTEST_MAIN_CFLAGS) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS)
tests_LDADD = \
  $(builddir)/libhexagonal.la \
  $(GTEST_MAIN_LIBS) \
  $(GTEST_LIBS) $(GLOG_LIBS)
tests_SOURCES = \
  coord_tests.cpp \
  pathfinder_tests.cpp \
  rangemap_tests.cpp \
  ring_tests.cpp

benchmarks_CXXFLAGS = $(BENCHMARK_CFLAGS) $(GLOG_CFLAGS)
benchmarks_LDADD = \
  $(builddir)/libbenchmain.la \
  $(builddir)/libhexagonal.la \
  $(BENCHMARK_LIBS) $(GLOG_LIBS)
benchmarks_SOURCES = \
  pathfinder_bench.cpp
