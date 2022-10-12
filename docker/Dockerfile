FROM alpine:latest AS builder
LABEL maintainer "pancake <pancake@nopcode.org>"

# Prevent build fails because of interactive scripts when compiling
ENV DEBIAN_FRONTEND noninteractive

# Dependencies and get, compile and test Iaito from master branch
RUN apk add --no-cache \
    bash \
    build-base \
    meson \
    ninja \
    curl \
    git \
    linux-headers \
    pkgconfig \
    patch \
    python3-dev \
    qt5-qtbase \
    qt5-qtsvg-dev \
    qt5-qttools-dev \
    qtchooser \
    qt5-qtbase-dev \
    unzip \
    zip \
    wget && \
    git clone https://github.com/radareorg/iaito.git /opt/iaito && \
    git clone --recurse-submodules https://github.com/radareorg/radare2 /opt/radare2 && \
    cd /opt/radare2 && ./sys/install.sh /usr && \
    cd /opt/iaito && sh configure && make

FROM alpine:latest AS runner

# Get the compiled Iaito, r2 libs and bins from the builder
COPY --from=builder /opt/iaito /opt/iaito
COPY --from=builder /opt/radare2 /opt/radare2
COPY --from=builder /usr/lib /usr/lib
COPY --from=builder /usr/share/radare2 /usr/share/radare2

# Add the dependencies we need for running
RUN apk add --no-cache \
    bash \
    libuuid \
    make \
    python3 \
    qt5-qtbase \
    shadow \
    su-exec && \
    cd /opt/radare2/binr && \
    make install && \
    make symstall install-symlink && \
    useradd r2 && \
    mkdir /var/sharedFolder && \
    mkdir -p /home/r2/.config/radare2 && \
    touch /home/r2/.radare2rc && \
    chown -R r2:r2 /var/sharedFolder && \
    chown -R r2:r2 /home/r2/

WORKDIR /home/r2
ADD entrypoint.sh /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
