# syntax=docker/dockerfile:1

# ---- build: compile, run the full test suite, keep the binaries ----
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake ninja-build git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY apps apps
COPY tests tests
COPY bench bench
COPY data data
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build \
    && ctest --test-dir /build --output-on-failure

# ---- runtime: replay binary, benchmarks and the Python client ----
FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 python3-matplotlib \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /build/apps/tickmatch_replay /build/bench/tickmatch_bench /usr/local/bin/
WORKDIR /opt/tickmatch
COPY client client
COPY data data
COPY scripts scripts
RUN useradd --create-home --uid 10001 tickmatch && mkdir -p /out && chown tickmatch /out
USER tickmatch
ENV REPLAY=/usr/local/bin/tickmatch_replay OUT=/out CLIENT_DIR=/opt/tickmatch/client MPLCONFIGDIR=/tmp
ENTRYPOINT ["scripts/demo.sh"]
