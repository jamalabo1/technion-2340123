FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    gdb \
    strace \
    procps \
    man-db \
    manpages-dev \
    linux-libc-dev \
    pkg-config \
    git \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Optional: create normal dev user.
# CLion may override the user when mounting files, but this is still useful manually.
ARG USERNAME=builder
ARG UID=1000
ARG GID=1000

RUN groupadd -g ${GID} ${USERNAME} || true && \
    useradd -m -u ${UID} -g ${GID} -s /bin/bash ${USERNAME} || true && \
    echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME}

WORKDIR /work