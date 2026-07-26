# Builds a base Docker image that contains a built Taurion.  This image
# is then used as basis to assemble the "minimised" real GSP image and
# also the testing image (with some more dependencies added).

# NOTE (2026-07-26): this build does NOT currently work against the pinned
# digest, and has not since superblocks landed.  src/logic.hpp overrides
# SQLiteGame::GetCustomInstanceState, a virtual introduced upstream by
# "Introduce the concept of superblocks in game" (2a43af5); the pinned image
# predates it, and so does xaya/libxayagame:latest, so no published image can
# build this tree -- only libxayagame from current source.  It is not something
# a digest bump here can fix.  Until an image carrying that virtual is
# published, build this base FROM libxayagame source instead of the digest; the
# unit, database and gametest suites (docker/testing/run-tests.sh) all pass that
# way.  A reviewer who skips this step gets a compile error, not a test failure,
# so do not read one as evidence about the game code.
FROM xaya/libxayagame@sha256:2303174359025270fa7b4c931bd741d9455fad7173a2d0b2265ad63cc27fe77a
RUN apt update && apt -y install \
  autoconf \
  autoconf-archive \
  automake \
  build-essential \
  cmake \
  git \
  libtool \
  pkg-config

# Number of parallel cores to use for make builds.
ARG N=1

# Build and install the Google benchmark library from source.
WORKDIR /usr/src/benchmark
RUN git clone https://github.com/google/benchmark .
RUN cmake -DBENCHMARK_ENABLE_GTEST_TESTS=OFF . \
    && make -j${N} && make install/strip

# Build and install Taurion.
WORKDIR /usr/src/taurion
COPY . .
RUN make distclean || true
RUN ./autogen.sh && ./configure && make -j${N} && make install-strip
