# Asset server

School project specification: [specifikace.md](specifikace.md)

This is an asset server for uploading and processing images. The main use-case is generating responsive variants of images for use on web pages. Each uploaded image can be converted to multiple different sizes and file formats. Additionally, files will be deduplicated using a hash of the uploaded data.

After you build and start the server (see below), the usage is quite simple: send requests to the `/api/upload?filename=image.jpg` endpoint with the image in the body of the request. Make sure to not use any HTTP application encoding like `application/x-www-form-urlencoded`, the body of the request must be just the raw bytes of the image, nothing else. I'm specifically mentioning this because with `curl`, you need to use `--data-binary @/path/to/image.jpg` instead of `--data`.

When you upload an image to the server, it will process the image according to your configuration (see the example configuration file `asset-server.cfg` for format conversion and resizing features) and save it to the filesystem*. The created file structure will look like this:

```raw
data/
  0123456789abcdef/
    image.png
    1200x1500/
      image.webp
      image.jpeg
    300x375/
      image.webp
      image.jpeg
    800x1000/
      image.webp
      image.jpeg
```

*: The codebase is designed to easily support multiple storage backends. However, currently only the filesystem backend is available.

Where `0123456789abcdef` is a substring of the SHA256 hash of this image, `image` is the filename you provided in the `?filename=` query parameter of the request, and the directories `1200x1500`, `300x375` and `800x1000` are the sizes of the generated variants - files in the subfolders will have this size. The `<filename>.<original.formats[0]>` file in the root of the image directory is the file that you uploaded, byte-for-byte identical without any processing applied. The server will return a JSON response with this content:

```json
{
  "filename": "image",
  "hash": "0123456789abcdef",
  "original": {
    "width": 1200,
    "height": 1500,
    "formats": ["png"]
  },
  "variants": [
    {
      "width": 1200,
      "height": 1500,
      "formats": ["webp", "jpeg"]
    },
    {
      "width": 300,
      "height": 375,
      "formats": ["webp", "jpeg"]
    },
    {
      "width": 800,
      "height": 1000,
      "formats": ["webp", "jpeg"]
    }
  ]
}
```

If any error happens, the server will return `{"error": "error.identifier"}` with a 4xx or 5xx HTTP status code. Possible errors are:

- `error.not_found`, `error.method_not_allowed`: invalid URL or request method, as per HTTP conventions
- `error.unauthorized`: the server is configured to require authentication, but credentials weren't correct
- `error.processing_timed_out`: processing the image took too long (the server is possibly overloaded)
- `error.payload_too_large`: uploaded data is larger than the configured allowed maximum
- `error.invalid_image`: the uploaded data is not a valid image of any supported format
- `error.bad_request`: an error was encountered while parsing the request, the request is most likely malformed
- `error.internal`: an internal error happened, please check the server logs for more information

This server does not provide any means of accessing the stored images. For that, you should use a separate service. You can find an example of a full deployment is is in the `example-deployment` folder - you can start it with `cd example-deployment && docker compose up -d`. This will start this server + Caddy, a reverse proxy and static file server with a simple example application. After it starts, you can open http://localhost:8000/ in your browser, and you should see a simple web page with a form for uploading images. The uploaded images will be stored in the `example-deployment/data/final` folder, and a small gallery will build up in your browser.

## Build and run

_Quickstart for Debian-based systems: run `sh scripts/debian-build.sh`. This will install dependencies as listed build and build the project._

_Quickstart with Docker: run `mkdir asset-data; sh scripts/docker-build-and-run.sh ./asset-data` to build the project and run it in a container._

The project should be easy to build on any Linux system, provided you have these development libraries available:

- Basic build tools: `git`, `cmake`, `make`
- A C++ compiler with support for C++20 or higher - tested with `g++` and `clang`
- Development headers for libraries:
  - `vips` for image operations
  - `magic` (sometimes also called `file`, as it's typically distributed with this command) for image format detection
  - `boost` for HTTP server
  - `openssl` for generating hashes

Here are dependency installation commands for a few popular Linux distributions:
- Debian: `apt install libvips-dev libmagic-dev libboost-dev libssl-dev cmake make git g++`
- Fedora: `dnf install vips-devel file-devel boost-devel openssl-devel cmake make git g++`
- Alpine: `apk add vips-dev file-dev boost-dev openssl-dev cmake make git g++`

MacOS users should be able install everything with `brew` and run this project without any problems, however, I unfortunately don't have a device to test this.

After installation of dependencies, simply checkout this repository and run the standard CMake build process:

```sh
git clone https://github.com/mvolfik/asset-server-cpp.git
cd asset-server-cpp

mkdir build
cd build
cmake ..
make
```

This will produce an executable named `asset-server` in your repository. To run it, you need to provide a configuration file. This repository contains an example configuration file that contains description of all available fields.

From the build folder, you can use the example configuration file with `./asset-server --config-file ../asset-server.cfg`. If you don't provide the `--config-file` argument, the server will look for a file named `asset-server.cfg` in the current working directory.

### Run-time dependencies

If you run the application on the same machine where you built it, you already have everything necessary. However, if you are building this elsewhere, and then copying it to a similar system without the project dependencies, you will need to install the shared libraries that this project links to: `libvips`, `libmagic`, `libssl` and `libstdc++`. The latter two are quite likely preinstalled.

This project has no specific hardware dependencies (libvips does not require a GPU). It has been tested on `x86_64` and `aarch64` CPU architectures, and should generally run on any platform supported by the dependencies.

### Docker build

Alternatively, if you have Docker/Podman installed, then you can run the server in a container without installing any dependencies on your host system.

Dockerfiles are provided in multiple variants, using Debian, Alpine or Fedora as base images, and using gcc or clang as the compiler as a demonstration of platform-independence. You can build the image with

```sh
docker build -t asset-server . -f Dockerfile.debian-gcc # use your preferred variant here
```

To run the image, you need to mount one (!) directory, which will contain two subdirectories - for temporary data, and for the final processed images. Due to the way Docker mounts work, these can't be two separate mounts.

For running in a container, you will need these changes to your `asset-server.cfg`:

```raw
storage.data_dir=./data/final
storage.temp_dir=./data/tmp
listen_host=0.0.0.0
```

Then run the container with

```sh
docker run --rm -it -p 8000:8000 -v ./data:/app/data -v ./asset-server.cfg:/app/asset-server.cfg asset-server
```

### Development build(s)

There are a few more .cpp files in the repository, used for unit testing some utility functions and for debugging some parts of the project. To have these build, setup the CMake build directory with  
`cmake -DASSET_SERVER_EXTRA_DEBUG=True ..`.

This also adds a build flag `-fsanitize=address` to the build, which requires `libasan-dev` as an additional dependency. This helps us check for memory leaks and other memory-related issues. (Aside: there's a fun use-after-free bug possible with shared_ptr, which doesn't get caught by this sanitizer. See `sandbox.cpp`.)

## Developer documentation (= implementation design)

The code is organized into a the following main components:

### `src/utils.hpp`, `src/server_state.hpp`

Utils contain definitions of various convenience functions and adapters for cumbersome APIs from some dependencies, server state is a cheaply copyable struct for data we need to pass to many places in the server. Nothing too interesting.

### `src/storage/`

Definition of the storage backend interface, and the filesystem-based implementation. The API uses a "staging" folder for preparation of the converted files, and then has a "commit" operation that should atomically move this folder into the data area. This has the advantage that the data area is always in a consistent state - it doesn't contain any half-written files or a folder with only some of the variants that should be generated.

### `src/config.hpp`

Pretty straightforward code to parse the configuration file and provide access to the values. Instantiates a storage backend based on `storage.type`, then forwards any `storage.*` values to it. At the end of parsing calls `backend.validate()` to allow the backend to check if it is ready for use.

### `src/thread_pool.hpp`

Since processing image files is computationally expensive, we want to utilize multiple threads. This module defines a simple thread pool and a task group. Task group allows us to easily manage thread pool tasks that are part of one bigger job (in our case the complete processing of an image uploaded to the server). Tasks from a group can spawn other tasks, and the owner of the task group can register a callback that will be called when all tasks in the group are done, even without knowing how many tasks will eventually be spawned.

### `src/http_connection.hpp`

Some boilerplate-y code for handling HTTP connections, parsing requests, verifying authentication and sending responses. After extracting everything necessary from the request, creates an `image_processor`, registers a callback for when the processing is done, and then simply returns. The Boost library is made for such asynchronous code - it holds this instance alive as long as the connection is alive. When the processing _finish_ callback is called, a response is sent to the connection.

### `src/image_processing.hpp`

Pretty much all of the _business logic_. Image processing is handled in multiple stages:

  1. `check_existence`: check if this image file has been uploaded before, else starts further processing.
  2. `load_image`: loads the image from the request into Vips, determines all the sizes that we need to generate and starts the resize tasks
  3. `resize`: a task running this function will resize the image to one size specification. From that, it starts subsequent tasks to convert the resized variant to the requested formats
  4. `save_to_format`: takes the resized variant, converts it to the requested format and saves it to the staging storage backend.

Required sequential dependencies of the individual stages are handled easily using the task group API: each task enqueues any subsequent tasks before it returns, so the group has always at least one pending task, until all tasks in the last stage finish.

After the last task finishes, the task group calls the registered `finalize()` callback. Here we can run anything that needs all the files already present in the temp folder (most importantly the `commit_staged_folder()` operation), and then call the callback that HTTP registered with ourselves, allowing it to send a response.

### Use-after-free safety

What complicates the code here a bit is the fact that `http_connection` can't be the only _owner_ of `image_processor`. The connection instance will be destroyed if the HTTP client that sent the request disconnects, or when the processing timeout runs out. But at that moment, some processing tasks might still be running, so we can't release their data, so we can't destroy `image_processor`.

So, to handle this safely, without risking any use-after-free bugs, `image_processor` is wrapped in `shared_ptr`, and a copy of this shared_ptr is stored in each task. This makes `image_processor` live as long as there's at least one task remaining, and `http_connection` doesn't need to manage it at all.

An extra fun part of this is making sure that the task group can safely call the finalize callback after the last task finishes, because `image_processor` is the direct owner of `task_group`, without any reference-counted pointers in-between. Luckily, the design of C++ lambdas helps us here quite a bit - the lambda holds the `shared_ptr<image_processor>`, even after it has been called. So even after the task returns, the lambda's associated data still exists, and therefore the task group has not been freed, and can call `on_finish()`.

### Upload race-condition safety

An interesting part of the processing logic is ensuring that the same image is not processed multiple times. Here's a verbose step-by-step description of the logic:

- calculate hash of received image data
- check if image with this hash is already saved; if yes, done
- lock `hashmap<hash, notifier=shared_ptr<atomic_bool>>`
- if entry for given hash exists:
  - get a reference to the notifier
  - unlock hashmap
  - sleep on the notifier (`atomic_bool.wait(true)`)
  - after wakeup, the file is ready, done
- else:
  - create entry with `make_shared(true)`, keep the reference
  - unlock hashmap
  - again, check if image with this hash is already saved, if no:
    - process the image in tmpdir
    - when done, atomically rename the built folder to target location
  - (regardless of whether we actually did the processing, or if we noticed that the result already exists:)
  - lock hashmap again
  - remove the entry
  - unlock hashmap
  - `*notifier = false`
  - `notifier.notify_all()`
  - done

The notifier is simply a bool (behind a shared_ptr, so that it doesn't move and can be accessed after the hashmap is unlocked) that starts at `true`, and is set to `false` when the image is ready, and all threads waiting on it are then woken up. (The value check/change is necesary, since [spurious wakeups may happen](https://en.cppreference.com/w/cpp/atomic/atomic/wait).)

The existence check after creating the hashmap entry **is necessary** - in a sense, the first existence check is just an optimization. Here's an example scenario, where the second check saves us from doing the same thing twice:

| request 1                                    | request 2                                             | request 3                         |
| -------------------------------------------- | ----------------------------------------------------- | --------------------------------- |
| exist check (false)                          |                                                       |                                   |
| lock                                         |                                                       |                                   |
| create entry with empty list of notifiers    |                                                       |                                   |
| unlock                                       |                                                       |                                   |
| exist check (false)                          | exist check (false)                                   | exist check (false)               |
| process image, rename to target folder       | ...                                                   | ...                               |
| lock                                         | ...                                                   | ...                               |
| notify registered notifiers and remove entry | ...                                                   | ...                               |
| unlock                                       | ...                                                   | ...                               |
| \<done>                                      | lock                                                  | ...                               |
|                                              | create entry with empty list of notifiers             | ...                               |
|                                              | unlock                                                | ...                               |
|                                              |                                                       | lock                              |
|                                              |                                                       | entry exists -> register notifier |
|                                              |                                                       | unlock and sleep                  |
|                                              | **exist check (true)** - here we saved ourselves work |                                   |
|                                              | lock                                                  |                                   |
|                                              | notify registered notifiers and remove entry          | (wakes up)                        |
|                                              | unlock                                                |                                   |
|                                              | \<done>                                               | \<done>                           |

### `src/main.cpp`

The entrypoint doesn't do anything unexpected - parses the command line, calls the config parser, initializes all the needed services (task group, Vips, the HTTP server), and then just keeps serving requests until a termination signal is received.

### Dockerfiles

The Dockerfiles use a pretty standard two-stage build pattern. In the `builder` stage, we install all the `-dev` dependencies, and build the binary. The final stage doesn't need `cmake`, the compiler, or any development headers, we just copy the binary there and install just the shared libraries.

## Testing

Simple test of functionality:

```sh
curl -s https://placecats.com/1000/2000 | curl -si "http://localhost:8000/api/upload?filename=cat.jpg" -X POST --data-binary @-
```

Interactive manual test: run the deployment and open http://localhost:8000/ in your browser. Upload a few images.

Unit tests of some utility functions (`src/test.cpp`):

```sh
mkdir -p build
cd build
cmake -DASSET_SERVER_EXTRA_DEBUG=True ..
make test
./test
```

End-to-end test of full functionality:

```sh
sh test/e2e_tests.sh
sh test/e2e_tests.sh --docker alpine-gcc # or any other variant
```

## License

The HTTP server, forming the core of the project, was kickstarted with an example from https://github.com/boostorg/beast/blob/9d05ef58d19672d91475e6be6f746d9bdb031362/example/http/server/small/http_server_small.cpp, which is licensed under the Boost license. Therefore it is easiest to license this project under the Boost license as well.

Copyright (c) 2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
Copyright (c) 2025 Matěj Volf

Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

The files `vendor/ada.*` are of the ada-url project, see https://github.com/ada-url/ada for licensing information.
