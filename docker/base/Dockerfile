# Builds a base Docker image that contains a built Taurion.  This image
# is then used as basis to assemble the "minimised" real GSP image and
# also the testing image (with some more dependencies added).

FROM xaya/libxayagame:0afa48e35b6d83095d85daa6b8923cec550f8508
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
