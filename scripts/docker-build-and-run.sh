#!/bin/sh

set -e

data_dir="$1"

if [ -z "$data_dir" ]; then
  echo "Usage: $0 <data_dir> [<config_file>]"
  echo "  <data_dir>    Directory to store data files (e.g., images, videos)"
  echo "  <config_file> Optional path to a config file (default: scripts/default_docker_config.cfg)"
  exit 1
fi

if [ ! -d "$data_dir" ]; then
  echo "Error: Data directory $data_dir does not exist."
  exit 1
fi

echo "Using data directory: $data_dir"

config_file="$2"

if [ -z "$config_file" ]; then
  echo "Warning: No config file provided. Using default config for running in Docker, which has no authentication"
  config_file="$(dirname "$0")/default_docker_config.cfg"
fi

docker build -t asset-server:latest "$(dirname "$0")/.." -f "$(dirname "$0")/../Dockerfile.debian-gcc"

docker run --rm --name asset-server -d -p 8000:8000 \
  -v "$(realpath "$data_dir"):/app/data" -v "$(realpath "$config_file"):/app/asset-server.cfg" \
  asset-server:latest

echo "Asset server is running. You can acess the api at http://localhost:8000, stop it with 'docker stop asset-server'."
echo "Tailing logs, press Ctrl+C to stop."
docker logs -f asset-server
