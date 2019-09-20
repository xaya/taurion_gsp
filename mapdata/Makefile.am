noinst_LTLIBRARIES = libmapdata.la
noinst_PROGRAMS = procmap

COMPRESSED = obstacledata.dat.xz regiondata.dat.xz
UNCOMPRESSED = $(COMPRESSED:%.xz=%)
CHECKSUMS = $(COMPRESSED:%.xz=%.sha512)
BLOBS = obstacles.bin regionxcoord.bin regionids.bin

EXTRA_DIST = $(COMPRESSED) $(CHECKSUMS)

BUILT_SOURCES = tiledata.cpp
CLEANFILES = $(UNCOMPRESSED) tiledata.cpp $(BLOBS)

libmapdata_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS)
libmapdata_la_LIBADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS)
libmapdata_la_SOURCES = \
  basemap.cpp \
  regionmap.cpp \
  tiledata.cpp \
  \
  blobs.s
noinst_HEADERS = \
  basemap.hpp basemap.tpp \
  dyntiles.hpp dyntiles.tpp \
  regionmap.hpp \
  tiledata.hpp \
  \
  dataio.hpp

check_PROGRAMS = tests benchmarks
TESTS = tests benchmarks

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GTEST_MAIN_CFLAGS) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS)
tests_LDADD = \
  $(builddir)/libmapdata.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GTEST_MAIN_LIBS) \
  $(GTEST_LIBS) $(GLOG_LIBS)
tests_SOURCES = \
  basemap_tests.cpp \
  dyntiles_tests.cpp \
  regionmap_tests.cpp \
  \
  dataio.cpp

benchmarks_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS) $(BENCHMARK_CFLAGS)
benchmarks_LDADD = \
  $(builddir)/libmapdata.la \
  $(top_builddir)/hexagonal/libbenchmain.la \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS) $(BENCHMARK_LIBS)
benchmarks_SOURCES = \
  dyntiles_bench.cpp \
  regionmap_bench.cpp

procmap_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
procmap_LDADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
procmap_SOURCES = procmap.cpp \
  dataio.cpp

$(UNCOMPRESSED): %: %.xz %.sha512
	xz -dc $< >$@
	sha512sum -c $(srcdir)/$*.sha512

tiledata.cpp $(BLOBS): $(UNCOMPRESSED) procmap$(EXEEXT)
	$(builddir)/procmap$(EXEEKT) \
	  --obstacle_input=obstacledata.dat \
	  --region_input=regiondata.dat \
	  --code_output=tiledata.cpp \
	  --obstacle_output=obstacles.bin \
	  --region_xcoord_output=regionxcoord.bin \
	  --region_ids_output=regionids.bin
	touch $(srcdir)/blobs.s
