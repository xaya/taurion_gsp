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
  character.cpp \
  database.cpp \
  schema.cpp
noinst_HEADERS = \
  character.hpp \
  database.hpp \
  schema.hpp

check_LTLIBRARIES = libdbtest.la
check_PROGRAMS = tests
TESTS = tests

libdbtest_la_CXXFLAGS = $(GTEST_CFLAGS) $(GLOG_CFLAGS) $(SQLITE3_CFLAGS)
libdbtest_la_LIBADD = \
  $(builddir)/libdatabase.la \
  $(GTEST_LIBS) $(GLOG_LIBS) $(SQLITE3_LIBS)
libdbtest_la_SOURCES = \
  dbtest.cpp
check_HEADERS = \
  dbtest.hpp

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS)
tests_LDADD = \
  $(builddir)/libdbtest.la \
  $(builddir)/libdatabase.la \
  $(top_builddir)/proto/libpxproto.la \
  $(GTEST_LIBS) $(GLOG_LIBS)
tests_SOURCES = \
  character_tests.cpp \
  database_tests.cpp \
  schema_tests.cpp

schema.cpp: schema_head.cpp schema.sql schema_tail.cpp
	cat $^ >$@
