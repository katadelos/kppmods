ARG NODE_VERSION=24.11.0
FROM node:${NODE_VERSION}-bookworm AS node
FROM ubuntu:24.04
COPY --from=node /usr/local/ /usr/local/
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake build-essential git librsvg2-bin patch python3 file default-jdk-headless \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
CMD ["bash"]
