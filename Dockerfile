# Builds a Docker image that contains tauriond, and that will run the GSP
# process if executed.

FROM xaya/charon AS build
RUN apk add --no-cache \
  autoconf \
  autoconf-archive \
  automake \
  build-base \
  cmake \
  gflags-dev \
  git \
  libtool \
  pkgconfig

# Build and install the Google benchmark library from source.
WORKDIR /usr/src/benchmark
RUN git clone https://github.com/google/benchmark .
RUN git clone https://github.com/google/googletest
RUN cmake . && make && make install/strip

# Build and install tauriond.
WORKDIR /usr/src/taurion
COPY . .
RUN make distclean || true
RUN ./autogen.sh && ./configure && make && make install-strip

# Collect the binary and required libraries.
WORKDIR /jail
RUN mkdir bin && cp /usr/local/bin/tauriond bin/
RUN cpld bin/tauriond lib64

# Construct final image.
FROM alpine
COPY --from=build /jail /usr/local/
ENV LD_LIBRARY_PATH "/usr/local/lib64"
LABEL description="Taurion game-state processor"
VOLUME ["/log", "/xayagame"]
ENV GLOG_log_dir "/log"
ENTRYPOINT [ \
  "/usr/local/bin/tauriond", \
  "--datadir=/xayagame", \
  "--enable_pruning=1000" \
]
CMD []
