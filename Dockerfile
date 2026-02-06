FROM archlinux:latest

LABEL maintainer="coio"
LABEL description="Development environment for coio C++20 async io library"

WORKDIR /workspace/coio

RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm \
    base-devel \
    gcc \
    clang \
    llvm \
    cmake \
    ninja \
    pkgconf \
    git \
    curl \
    wget \
    ca-certificates \
    gdb \
    lldb \
    liburing \
    vim \
    && pacman -Scc --noconfirm

RUN git clone --depth 1 https://github.com/NVIDIA/stdexec.git /tmp/stdexec \
    && cd /tmp/stdexec \
    && cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DSTDEXEC_BUILD_EXAMPLES=OFF \
        -DSTDEXEC_BUILD_TESTS=OFF \
    && cmake --build build -j$(nproc) \
    && cmake --install build \
    && rm -rf /tmp/stdexec

COPY . /workspace/coio

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCOIO_BUILD_EXAMPLES=ON \
    -DCOIO_SENDERS_BACKEND=NVIDIA \
    && cmake --build build -j$(nproc)

EXPOSE 8080 8086

CMD ["/bin/bash"]