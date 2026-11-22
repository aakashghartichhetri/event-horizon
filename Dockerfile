# Two-stage build: no compiler or development headers in the runtime image.
FROM ubuntu:24.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake ninja-build libboost1.83-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
# Every Docker build configures CMake from scratch. Do not let Ninja regenerate
# the manifest during the same build; this also avoids clock-skew rebuild loops.
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_SUPPRESS_REGENERATION=ON \
      -DEH_BUILD_TESTS=OFF -DEH_WITH_GTEST=OFF -DEH_BUILD_BENCHMARKS=OFF \
    && cmake --build /build -j 2

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /build/event-horizon-server /usr/local/bin/event-horizon-server
COPY --from=builder /build/event-horizon /usr/local/bin/event-horizon
COPY data ./data
COPY web ./web
USER 10001:10001
EXPOSE 8080
CMD ["event-horizon-server", "--host", "0.0.0.0", "--port", "8080", "--data-dir", "data", "--web-dir", "web"]
