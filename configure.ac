AC_INIT([tauriond], [0.3])
AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AM_PROG_AS
AM_PROG_AR
AM_PATH_PYTHON([3.7])

LT_INIT

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX([14], [noext])
AX_CHECK_COMPILE_FLAG([-Wall], [CXXFLAGS="${CXXFLAGS} -Wall"])
AX_CHECK_COMPILE_FLAG([-Werror], [CXXFLAGS="${CXXFLAGS} -Werror"])
AX_CHECK_COMPILE_FLAG([-pedantic], [CXXFLAGS="${CXXFLAGS} -pedantic"])
AX_CHECK_COMPILE_FLAG([-Wno-deprecated],
                      [CXXFLAGS="${CXXFLAGS} -Wno-deprecated"])

# Windows defines ERROR, which requires us to tell glog to not define
# it as abbreviated log severity (LOG(ERROR) still works, though, and
# that is all that we actually use in the code).
# See https://hpc.nih.gov/development/glog.html.
CXXFLAGS="${CXXFLAGS} -DGLOG_NO_ABBREVIATED_SEVERITIES"

# The code includes some sanity checks / assertions that noticably slow down
# general block-processing performance.  Thus enabling them is optional,
# and should be used only for testing purposes.
AC_ARG_ENABLE([slow-asserts],
  AS_HELP_STRING([--enable-slow-asserts],
                 [Enable slow consistency checks for testing]))
AS_IF([test "x$enable_slow_asserts" = "xyes"], [
  CXXFLAGS="${CXXFLAGS} -DENABLE_SLOW_ASSERTS"
], [
  enable_slow_asserts="no"
])

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES([XAYAGAME], [libxayautil libxayagame])
PKG_CHECK_MODULES([CHARON], [charon])
PKG_CHECK_MODULES([SQLITE3], [sqlite3])
PKG_CHECK_MODULES([JSON], [jsoncpp])
PKG_CHECK_MODULES([GLOG], [libglog])
PKG_CHECK_MODULES([GFLAGS], [gflags])
PKG_CHECK_MODULES([GTEST], [gmock gtest])
PKG_CHECK_MODULES([GTEST_MAIN], [gtest_main])
PKG_CHECK_MODULES([PROTOBUF], [protobuf])

# Make sure that pkg-config provides the right libraries for some dependencies
# that may only be available statically.  For context, see:
# https://bugs.freedesktop.org/show_bug.cgi?id=19541.
PKG_CONFIG="${PKG_CONFIG} --static"
PKG_CHECK_MODULES([BENCHMARK], [benchmark])

# It seems that GMP does not support pkg-config, so we have to check for it
# and set up the variables manually.
AC_CHECK_LIB(gmp, __gmpz_init, ,
  [AC_MSG_ERROR([GNU MP not found, see https://gmplib.org/])])
AC_SUBST(GMP_CFLAGS, )
AC_SUBST(GMP_LIBS, -lgmp)

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
echo "Slow assertions: ${enable_slow_asserts}"
echo "CXXFLAGS: ${CXXFLAGS}"
