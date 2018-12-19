noinst_LTLIBRARIES = libmapdata.la
noinst_PROGRAMS = procmap

libmapdata_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS)
libmapdata_la_LIBADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS)
libmapdata_la_SOURCES = \
  basemap.cpp
noinst_HEADERS = \
  basemap.hpp \
  \
  dataio.hpp

procmap_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
procmap_LDADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
procmap_SOURCES = procmap.cpp \
  dataio.cpp
