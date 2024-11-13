# Asset server

Specifikace: [specifikace.md](specifikace.md)

Implementation of parallelism, to avoid processing same image twice:

- calculate hash of received image data
- check if image with this hash is already saved; if yes, done
- lock hashmap<hash, vec<notifier>>
- if entry for given hash exists:
  - register a notifier
  - unlock hashmap
  - sleep on notifier
  - after wakeup, the file is ready, done
- else:
  - create entry with empty vec
  - unlock hashmap
  - again, check if image with this hash is already saved, if no:
    - process the image in tmpdir
    - when done, atomically rename the built folder to target location
  - (regardless of whether we actually did the processing, or if we noticed that the result already exists:)
  - lock hashmap again
  - notify all registered notifiers
  - remove the entry
  - unlock
  - done

The existence check after creating the hashmap entry **is necessary**, in a sense, the first existence check is just an optimization to decrease lock contention on the hashmap. Here's an example scenario, where the second check saves us from doing the same thing twice:

| request 1 | request 2 | request 3 |
|-----------|-----------|-----------|
| exist check (false) |||
| lock |||
| create entry with empty list of notifiers |||
| unlock |||
| exist check (false) | exist check (false) | exist check (false) |
| process image, rename to target folder | ... | ... |
| lock | ... | ... |
| notify registered notifiers and remove entry | ... | ... |
| unlock | ... | ... |
| \<done> | lock | ... |
|| create entry with empty list of notifiers | ... |
|| unlock | ... |
||| lock |
||| entry exists -> register notifier |
||| unlock and sleep |
|| exist check (true) ||
|| lock ||
|| notify registered notifiers and remove entry | (wakes up) |
|| unlock ||
|| \<done> | \<done> |


Build and run on Debian:

```sh
apt install libmagick++-dev libboost-dev cmake g++ make --no-install-recommends -y
mkdir build
cd build
cmake ..
make -j
./asset-server
```

Repository also contains working two Dockerfiles, using Debian and Alpine as base images.

Windows: untested so far. Both Magick and Boost say Windows is supported, but I have no idea about the Cmake integration will work.

<!-- ## Testing

`curl -i 'http://0.0.0.0:8000/api/upload?filename=any_filename_that_you_choose_suffix_doesnt_matter.png' -X POST --data-binary @$HOME/Pictures/image.jpg`

--- -->

## License

The HTTP server, forming the core of the project, was kickstarted with an example from https://github.com/boostorg/beast/blob/9d05ef58d19672d91475e6be6f746d9bdb031362/example/http/server/small/http_server_small.cpp, which is licensed under the Boost license. Therefore it is easiest to license this project under the Boost license as well.

Copyright (c) 2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
Copyright (c) 2024 Matěj Volf

Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

The files `vendor/ada.*` are of the ada-url project, see https://github.com/ada-url/ada for licensing information.
