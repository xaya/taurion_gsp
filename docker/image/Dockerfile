# Builds a Docker image that contains the GSP for running in production.
# This is based on the pre-built "base" image from docker/base, which
# must be built before.

FROM tn-base AS build

# Collect the binary and required libraries.
WORKDIR /jail
RUN mkdir bin && cp /usr/local/bin/tauriond bin/
RUN for b in bin/*; do cpld $b lib64; done

# Add extra files needed.
COPY docker/image/entrypoint.sh bin/

# Assemble the final image.
FROM debian:13-slim
COPY --from=build /jail /usr/local/
ENV LD_LIBRARY_PATH="/usr/local/lib64"
ENV XAYAGAME_DIR="/xayagame"
ENV GLOG_log_dir="/log"
RUN mkdir -p /log /xayagame
LABEL description="Taurion GSP"
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
