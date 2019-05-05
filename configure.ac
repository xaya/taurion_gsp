AC_INIT([tauriond], [0.1])
AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AM_PROG_AS
AM_PROG_AR

LT_INIT

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX([14], [noext])
AX_CHECK_COMPILE_FLAG([-Wall], [CXXFLAGS+=" -Wall"])
AX_CHECK_COMPILE_FLAG([-Werror], [CXXFLAGS+=" -Werror"])
AX_CHECK_COMPILE_FLAG([-pedantic], [CXXFLAGS+=" -pedantic"])
AX_CHECK_COMPILE_FLAG([-Wno-deprecated], [CXXFLAGS+=" -Wno-deprecated"])

# Windows defines ERROR, which requires us to tell glog to not define
# it as abbreviated log severity (LOG(ERROR) still works, though, and
# that is all that we actually use in the code).
# See https://hpc.nih.gov/development/glog.html.
CXXFLAGS+=" -DGLOG_NO_ABBREVIATED_SEVERITIES"

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES([XAYAGAME], [libxayautil libxayagame])
PKG_CHECK_MODULES([SQLITE3], [sqlite3])
PKG_CHECK_MODULES([JSON], [jsoncpp])
PKG_CHECK_MODULES([GLOG], [libglog])
PKG_CHECK_MODULES([GTEST], [gmock gtest])
PKG_CHECK_MODULES([GTEST_MAIN], [gtest_main])
PKG_CHECK_MODULES([PROTOBUF], [protobuf])

# Make sure that pkg-config provides the right libraries for some dependencies
# that may only be available statically (e.g. gflags).  For context, see:
# https://bugs.freedesktop.org/show_bug.cgi?id=19541.
PKG_CONFIG="${PKG_CONFIG} --static"
PKG_CHECK_MODULES([BENCHMARK], [benchmark])
PKG_CHECK_MODULES([GFLAGS], [gflags])

AC_CONFIG_FILES([
  Makefile \
  database/Makefile \
  gametest/Makefile \
  hexagonal/Makefile \
  mapdata/Makefile \
  proto/Makefile \
  src/Makefile
])
AC_OUTPUT

echo
echo "CXXFLAGS: ${CXXFLAGS}"
