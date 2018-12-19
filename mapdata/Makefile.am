noinst_PROGRAMS = procmap

procmap_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
procmap_LDADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
procmap_SOURCES = procmap.cpp
