# Builds a Docker image that contains tauriond, and that will run the GSP
# process if executed.

FROM xaya/charon AS build
RUN apt-get update && apt-get install -y \
  autoconf \
  autoconf-archive \
  automake \
  cmake \
  git \
  libtool \
  pkg-config \
  protobuf-compiler

# Build and install the Google benchmark library from source.
WORKDIR /usr/src/benchmark
RUN git clone https://github.com/google/benchmark .
RUN git clone https://github.com/google/googletest
RUN cmake . && make && make install

# Build and install tauriond.
WORKDIR /usr/src/taurion
COPY . .
RUN ./autogen.sh && ./configure && make && make install

# Copy the stuff we built to the final image.
FROM xaya/charon
COPY --from=build /usr/local /usr/local/
RUN ldconfig
LABEL description="Taurion game-state processor"

# Define the runtime environment for the GSP process.
RUN useradd taurion \
  && mkdir -p /var/lib/xayagame && mkdir -p /var/log/taurion \
  && chown taurion:taurion -R /var/lib/xayagame /var/log/taurion
USER taurion
VOLUME ["/var/lib/xayagame", "/var/log/taurion"]
ENV GLOG_log_dir /var/log/taurion
ENTRYPOINT [ \
  "/usr/local/bin/tauriond", \
  "--datadir=/var/lib/xayagame", \
  "--enable_pruning=1000" \
]
CMD []
