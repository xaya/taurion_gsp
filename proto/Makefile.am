noinst_LTLIBRARIES = libpxproto.la

PROTOS = character.proto geometry.proto
EXTRA_DIST = $(PROTOS)

BUILT_SOURCES = $(PROTOS:.proto=.pb.h)
CLEANFILES = $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc)

libpxproto_la_CXXFLAGS = $(PROTOBUF_CFLAGS)
libpxproto_la_LIBADD = $(PROTOBUF_LIBS)
libpxproto_la_SOURCES = $(PROTOS:.proto=.pb.cc)
noinst_HEADERS = $(PROTOS:.proto=.pb.h)

%.pb.h %.pb.cc: $(srcdir)/%.proto
	protoc --cpp_out=. "$<"
