noinst_LTLIBRARIES = libdatabase.la

EXTRA_DIST = schema.sql schema_head.cpp schema_tail.cpp
CLEANFILES = schema.cpp

libdatabase_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GLOG_CFLAGS) $(SQLITE3_CFLAGS) $(PROTOBUF_CFLAGS)
libdatabase_la_LIBADD = \
  $(top_builddir)/hexagonal/libhexagonal.la \
  $(top_builddir)/proto/libpxproto.la \
  $(GLOG_LIBS) $(SQLITE3_LIBS) $(PROTOBUF_LIBS)
libdatabase_la_SOURCES = \
  schema.cpp
noinst_HEADERS = \
  schema.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS)
tests_LDADD = \
  $(builddir)/libdatabase.la \
  $(GTEST_LIBS) $(GLOG_LIBS)
tests_SOURCES = \
  schema_tests.cpp \
  \
  dbtest.cpp
check_HEADERS = \
  dbtest.hpp

schema.cpp: schema_head.cpp schema.sql schema_tail.cpp
	cat $^ >$@
