---
sidebar_position: 7
---

# Docker Deployment

Zedra provides Docker images for running the `zedra_ros` bridge node in containerized environments.

## Pre-built Images

Images are available on the [GitHub Container Registry](https://github.com/vselvarajijay/zedra/pkgs/container/zedra):

```bash
docker pull ghcr.io/vselvarajijay/zedra:latest
```

### Tags

| Tag | Description |
|-----|-------------|
| `latest` | Latest build from `main` branch |
| `v1.0.0` | Specific release version |
| `sha-<short>` | Specific commit SHA |

Images are **multi-arch**: `linux/amd64` and `linux/arm64` (Apple Silicon compatible).

## Running the Bridge

```bash
docker run --rm ghcr.io/vselvarajijay/zedra:latest
```

## Docker Compose

The included `docker-compose.yml` defines two services:

### Bridge Only

```bash
docker compose up
```

This starts the `zedra_ros` bridge node.

### Full Stack (with ClickHouse)

```bash
docker compose --profile clickhouse up
```

This starts:
- **`zedra`** — the bridge node
- **`clickhouse`** — ClickHouse server for event persistence
- **`clickhouse-init`** — runs the schema migration on first start

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ZEDRA_PERSIST_EVENTS` | `0` | Set to `1` to enable ClickHouse persistence |
| `CLICKHOUSE_HOST` | `clickhouse` | ClickHouse hostname |
| `CLICKHOUSE_PORT` | `9000` | ClickHouse native port |

## Building the Image Locally

```bash
docker build -t zedra .
docker run --rm zedra
```

The Dockerfile builds on `osrf/ros:kilted-desktop` and compiles `zedra_ros` with `colcon`.

## Multi-Arch Build

CI builds multi-arch images using Docker Buildx:

```bash
docker buildx create --use
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  -t ghcr.io/vselvarajijay/zedra:latest \
  --push .
```

## Testing the Docker Image

```bash
./scripts/docker_test.sh
```

This script builds the image, runs `zedra_ros` tests inside the container, and prints results.

## ClickHouse Schema

The ClickHouse schema is initialized by `scripts/migrations/001_init.sql`. It creates a table for persisted events with columns for `tick`, `tie_breaker`, `type`, and `payload`.

## CI/CD

On every push to `main` and on releases, the CI pipeline:
1. Builds and tests `zedra_core` with CMake
2. Builds and tests `zedra_ros` in the ROS 2 container
3. Builds and pushes the multi-arch Docker image to GHCR

See [`.github/workflows/cmake-single-platform.yml`](https://github.com/vselvarajijay/zedra/blob/main/.github/workflows/cmake-single-platform.yml) for details.
