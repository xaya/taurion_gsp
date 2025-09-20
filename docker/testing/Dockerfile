# Builds an image that contains all the dependencies to run Taurion
# GSP tests inside Docker.
# This is based on the "base" image from docker/base.

FROM tn-base
RUN apt update && apt -y install \
  curl \
  libboost-all-dev \
  libmariadb-dev \
  libunivalue-dev \
  libwebsocketpp-dev \
  python3 \
  virtualenv

# Number of parallel cores to use for make builds.
ARG N=1

# Install Foundry.  It seems pretty hard to get "foundryup" to run inside
# Docker, so we just download and manually install a specific release.
ARG FOUNDRY_RELEASE="nightly-1186156961f648273299ac067d098ee0e33a0d7e"
WORKDIR /usr/local/bin
RUN curl -L "https://github.com/foundry-rs/foundry/releases/download/${FOUNDRY_RELEASE}/foundry_nightly_linux_amd64.tar.gz" | tar zxv

# Install Python packages required for running our tests.
WORKDIR /usr/src/taurion
RUN virtualenv venv && . venv/bin/activate && pip3 install -r requirements.txt

# Build and install mypp.
ARG MYPP_COMMIT="c3895a6b157d1cb68e9d9c6835ec11b01278312c"
WORKDIR /usr/src/mypp
RUN git clone https://github.com/xaya/mypp . \
  && git checkout ${MYPP_COMMIT}
RUN ./autogen.sh && ./configure && make -j${N} && make install-strip

# Build and install Xaya X.
ARG XAYAX_COMMIT="e3db56b2a44213f0bef5c88eff63ce764b6cd1c1"
WORKDIR /usr/src/xayax
RUN git clone https://github.com/xaya/xayax . \
  && git checkout ${XAYAX_COMMIT}
RUN ./autogen.sh && ./configure
RUN make -C eth/solidity
# The build itself needs Python web3 as well, and we reuse the Taurion
# venv for that (which includes xayax, so all our requirements as well).
RUN . /usr/src/taurion/venv/bin/activate \
    && make -j${N} && make install-strip

# Build Taurion test binaries.
WORKDIR /usr/src/taurion
RUN make build-tests -j${N}

# We do not actually run the tests, but provide the image ready-made
# for running with a simple command as a follow-up operation.
ENV N="${N}"
COPY docker/testing/run-tests.sh /usr/local/bin/
