# The image every `make` goal compiles and runs in: Native's SDL2 SDK plus
# OpenSSL's headers, which Blaise links for TLS (docs/notes/TLS.md).
# The Makefile and platforms.py tag the image with a hash of this file, so a
# change here builds a new image on the next run.
FROM wischner/gcc-x86_64-linux-sdl:1.3.0
USER root
RUN apt-get update -q \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*
