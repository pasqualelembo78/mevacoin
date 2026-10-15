# syntax=docker/dockerfile:1
#
# MevaCoin — immagine daemon/wallet-rpc/explorer.
#
# Build (dal root del repo):
#   docker build --target daemon      -t mevacoin/mevacoind:latest .
#   docker build --target wallet-rpc  -t mevacoin/wallet-rpc:latest .
#   docker build --target explorer    -t mevacoin/explorer:latest .
#
# Oppure, con docker compose:  docker compose up -d
#
# Nota: la compilazione completa di mevacoind e' pesante. Il layer "builder"
# e' condiviso tra i target, quindi Docker lo compila una volta sola.

FROM ubuntu:22.04 AS base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config git ca-certificates ccache \
        libssl-dev libzmq3-dev libsodium-dev libunwind-dev liblzma-dev \
        libreadline-dev libexpat1-dev libusb-1.0-0-dev libhidapi-dev \
        libprotobuf-dev protobuf-compiler libudev-dev libunbound-dev \
        libboost-all-dev libpgm-dev libnorm-dev libcurl4-openssl-dev \
        libfmt-dev \
    && rm -rf /var/lib/apt/lists/*

# ── Builder: compila mevacoind, wallet-rpc e l'explorer ──────────────────────
FROM base AS builder
ARG BUILD_JOBS
WORKDIR /src
COPY . .

# La build tree usa il layout nativo (build/Linux/mevacoin/release) cosi' che
# l'explorer trovi le librerie statiche esattamente dove le cerca FindMonero.
RUN set -eux; \
    JOBS="${BUILD_JOBS:-$(nproc)}"; \
    cmake -S /src -B /src/build/Linux/mevacoin/release \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DBUILD_DOCUMENTATION=OFF \
        -DARCH="native"; \
    cmake --build /src/build/Linux/mevacoin/release \
        --target daemon wallet_rpc_server -j"${JOBS}"

# Block explorer (xmrblocks), linkato staticamente alla build di MevaCoin.
RUN set -eux; \
    JOBS="${BUILD_JOBS:-$(nproc)}"; \
    cmake -S /src/explorer -B /src/explorer/build \
        -DMONERO_DIR=/src \
        -DMONERO_BUILD_DIR=/src/build/Linux/mevacoin/release \
        -DCMAKE_BUILD_TYPE=Release; \
    cmake --build /src/explorer/build -j"${JOBS}"

# ── Runtime condiviso (librerie, senza toolchain) ───────────────────────────
FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl \
        libboost-chrono1.74.0 libboost-filesystem1.74.0 \
        libboost-program-options1.74.0 libboost-regex1.74.0 \
        libboost-serialization1.74.0 libboost-thread1.74.0 \
        libssl3 libzmq5 libsodium23 libunwind8 liblzma5 libreadline8 \
        libusb-1.0-0 libhidapi-libusb0 libunbound8 libprotobuf23 libfmt8 \
        libevent-2.1-7 libgmp10 libicu70 libnettle8 libnorm1 libpgm-5.3-0 \
        libudev1 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --shell /usr/sbin/nologin mevacoind

# ── Target: mevacoind ───────────────────────────────────────────────────────
FROM runtime AS daemon
COPY --from=builder /src/build/Linux/mevacoin/release/bin/mevacoind /usr/local/bin/mevacoind
USER mevacoind
WORKDIR /home/mevacoind
ENV MEVACOIN_DATA=/home/mevacoind/.mevacoin
VOLUME ["/home/mevacoind/.mevacoin"]
# mainnet 18080/18081 · testnet 28080/28081 · stagenet 38080/38081
EXPOSE 18080 18081 28080 28081 38080 38081

# ── Target: wallet-rpc ──────────────────────────────────────────────────────
FROM runtime AS wallet-rpc
COPY --from=builder /src/build/Linux/mevacoin/release/bin/mevacoin-wallet-rpc /usr/local/bin/mevacoin-wallet-rpc
USER mevacoind
WORKDIR /home/mevacoind
VOLUME ["/home/mevacoind/wallets"]
EXPOSE 18083
ENTRYPOINT ["mevacoin-wallet-rpc"]

# ── Target: explorer ────────────────────────────────────────────────────────
FROM runtime AS explorer
COPY --from=builder /src/explorer/build/xmrblocks /usr/local/bin/xmrblocks
COPY --from=builder /src/explorer/build/templates /app/templates
USER mevacoind
WORKDIR /app
EXPOSE 18084
ENTRYPOINT ["xmrblocks"]
CMD ["--bc-path=/home/mevacoind/.mevacoin/lmdb", \
     "--daemon-url=mevacoind:18081", "--port=18084", \
     "--enable-json-api", "--enable-autorefresh-option", \
     "--no-blocks-on-index=10", "--mempool-refresh-time=5"]

# Default target
FROM daemon AS final
ENTRYPOINT ["mevacoind"]
CMD ["--non-interactive", "--data-dir", "/home/mevacoind/.mevacoin", \
     "--p2p-bind-ip", "0.0.0.0", "--p2p-bind-port", "18080", \
     "--rpc-bind-ip", "0.0.0.0", "--rpc-bind-port", "18081", \
     "--confirm-external-bind"]
