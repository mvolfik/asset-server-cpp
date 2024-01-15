# Asset server

Tech demo as of right now.

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

Windows: untested. Magick says it works under Cygwin.

## Testing

`curl -i 'http://0.0.0.0:8000/api/upload?filename=any_filename_that_you_choose_suffix_doesnt_matter.png' -X POST --data-binary @$HOME/Pictures/image.jpg`

---

## License

The HTTP server, forming the core of the project, was kickstarted with an example from https://github.com/boostorg/beast/blob/9d05ef58d19672d91475e6be6f746d9bdb031362/example/http/server/small/http_server_small.cpp, which is licensed under the Boost license. Therefore it is easiest to license this project under the Boost license as well.

Copyright (c) 2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
Copyright (c) 2024 Matěj Volf

Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
