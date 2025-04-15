#!/bin/sh

set -e

fail() {
  echo "FAIL: $1"
  exit 1
}

prepare_local() {
  echo "Building the project"
  cmake "$src"
  make -j 4 asset-server

  mkdir -p "$data_dir"

  echo "Running the server, logs will be in $dir/server_log"
  ./asset-server --config-file "$src/test/config1.cfg" > server_log 2>&1 &
  pid=$!

  sleep 1

  # check the server didn't die on startup
  if ! ps -p $pid > /dev/null; then
    echo "Server failed to start"
    exit 1
  fi

  trap "kill $pid" EXIT
}

prepare_docker() {
  image_name="asset-server-$(tr -dc a-z0-9 < /dev/urandom | head -c 13)"
  dockerfile="Dockerfile.$1"
  echo "Building the Docker image from $dockerfile as $image_name"
  docker build -t "$image_name" -f "$src/$dockerfile" "$src"

  echo "Running the Docker container, logs will be in $dir/server_log"
  docker run --rm --name "$image_name" -p 8000:8000 \
    -v "$dir/data:/app/data" -v "$src/test/docker_config.cfg:/app/asset-server.cfg" \
    "$image_name" > "server_log" 2>&1 &
  pid=$!

  sleep 1

  # check the server didn't die on startup
  if ! docker ps | grep -q "$image_name"; then
    echo "Server failed to start"
    exit 1
  fi

  trap "docker stop "$image_name" > /dev/null" EXIT
}

simple_test() {
  http_code="$1"
  response_text="$2"
  shift 2

  out=$(curl -s -o "$dir/resp" -w "%{http_code}\n" "$@")
  [ "$out" = "$http_code" ] || fail "Expected $http_code, got $out"
  [ -f "$dir/resp" ] || fail "Response file not found"
  grep -q "$response_text" "$dir/resp" || fail "Expected $response_text in response, response:
---
$(cat "$dir/resp")
---"
}

data_dir="./data/final"
src=$(realpath "$(dirname "$0")/..")
dir=$(mktemp -d)
cd "$dir"

if [ "$1" = "--docker" ]; then
  if [ -z "$2" ]; then
    echo "Usage: $0 --docker <dockerfile variant>"
    echo "Where variant is of <alpine|debian|fedora>-<gcc|clang>"
    exit 1
  fi
  prepare_docker "$2"
else
  prepare_local
fi

echo "Testing unknown path"
simple_test 404 '"error.not_found"' "http://localhost:8000/api/unknown"

echo "Testing missing filename"
simple_test 400 '"error.missing_filename"' -X POST "http://localhost:8000/api/upload"

echo "Testing unauthenticated access"
simple_test 401 '"error.unauthorized"' -X POST "http://localhost:8000/api/upload?filename=image1.jpg"

echo "Testing incorrectly authenticated access"
simple_test 401 '"error.unauthorized"' -X POST "http://localhost:8000/api/upload?filename=image1.jpg" -H "Authorization: Bearer wrong_token"

echo "Testing empty upload"
simple_test 400 '"error.invalid_image"' -X POST "http://localhost:8000/api/upload?filename=image1.jpg" -H "Authorization: Bearer testing_token"

echo "Testing upload of file bigger than limit"
dd if=/dev/zero of=./testfile bs=1M count=40 2> /dev/null
simple_test 413 '"error.payload_too_large"' -X POST "http://localhost:8000/api/upload?filename=image1.jpg" -H "Authorization: Bearer testing_token" --data-binary "@$dir/testfile"

echo "Testing upload of malformed file"
dd if=/dev/zero of=./testfile bs=1M count=3 2> /dev/null
simple_test 400 '"error.invalid_image"' -X POST "http://localhost:8000/api/upload?filename=image1.jpg" -H "Authorization: Bearer testing_token" --data-binary "@$dir/testfile"

[ "$(ls -A "$data_dir")" = "" ] || fail "Data directory is not empty after set of failed requests"

echo "Testing happy path"
out=$(curl -s -o "$dir/resp" -w "%{http_code}\n" -X POST "http://localhost:8000/api/upload?filename=image1.jpg" -H "Authorization: Bearer testing_token" --data-binary "@$src/test/testdata/image1.jpg")
[ "$out" = "200" ] || fail "Expected 200, got $out"
[ -f "$dir/resp" ] || fail "Response file not found"
diff "$dir/resp" "$src/test/testdata/image1_response.json" || fail "Response does not match expected response"
find "$data_dir" -type f | sort > "$dir/files.txt"
diff "$dir/files.txt" "$src/test/testdata/image1_files.txt" || fail "List of generated files does not match"

subdir="$(tr < "$dir/resp" , "
" | grep '"hash"' | cut -d'"' -f4)"
diff "$data_dir/$subdir/image1.jpeg" "$src/test/testdata/image1.jpg" || fail "Server does not preserve uploaded file"

echo "Testing upload of existing file with different name"
out=$(curl -s -o "$dir/resp" -w "%{http_code}\n" -X POST "http://localhost:8000/api/upload?filename=image3.png" -H "Authorization: Bearer testing_token" --data-binary "@$src/test/testdata/image1.jpg")
[ "$out" = "200" ] || fail "Expected 200, got $out"
[ -f "$dir/resp" ] || fail "Response file not found"
diff "$dir/resp" "$src/test/testdata/image1_response_existing.json" || fail "Response does not match expected response"
find "$data_dir" -type f | sort > "$dir/files.txt"
diff "$dir/files.txt" "$src/test/testdata/image1_files.txt" || fail "List of generated files does not match"

echo "=== ALL TESTS PASSED ==="