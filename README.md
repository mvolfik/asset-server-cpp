# Asset server

Specifikace: [specifikace.md](specifikace.md)

## Build and run


## Upload race-condition safety

Implementation of parallelism, to avoid processing same image twice:

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

The notifier is simply a bool (behind a shared_ptr, so that it a) doesn't move and b) can be accessed after the hashmap is unlocked) that starts at `true`, and is set to `false` when the image is ready, and all threads waiting on it are then woken up. (The value check/change is necesary, since [spurious wakeups may happen](https://en.cppreference.com/w/cpp/atomic/atomic/wait).)

The existence check after creating the hashmap entry **is necessary** - in a sense, the first existence check is just an optimization. Here's an example scenario, where the second check saves us from doing the same thing twice:

| request 1                                    | request 2                                    | request 3                         |
| -------------------------------------------- | -------------------------------------------- | --------------------------------- |
| exist check (false)                          |                                              |                                   |
| lock                                         |                                              |                                   |
| create entry with empty list of notifiers    |                                              |                                   |
| unlock                                       |                                              |                                   |
| exist check (false)                          | exist check (false)                          | exist check (false)               |
| process image, rename to target folder       | ...                                          | ...                               |
| lock                                         | ...                                          | ...                               |
| notify registered notifiers and remove entry | ...                                          | ...                               |
| unlock                                       | ...                                          | ...                               |
| \<done>                                      | lock                                         | ...                               |
|                                              | create entry with empty list of notifiers    | ...                               |
|                                              | unlock                                       | ...                               |
|                                              |                                              | lock                              |
|                                              |                                              | entry exists -> register notifier |
|                                              |                                              | unlock and sleep                  |
|                                              | exist check (true)                           |                                   |
|                                              | lock                                         |                                   |
|                                              | notify registered notifiers and remove entry | (wakes up)                        |
|                                              | unlock                                       |                                   |
|                                              | \<done>                                      | \<done>                           |



Repository also contains working two Dockerfiles, using Debian and Alpine as base images.

Windows: untested so far. libvips C++ bindings will likely need to be recompiled from source, I am deferring this for later.

<!-- ## Testing

`curl -i 'http://0.0.0.0:8000/api/upload?filename=any_filename_that_you_choose_suffix_doesnt_matter.png' -X POST --data-binary @$HOME/Pictures/image.jpeg`

--- -->

## License

The HTTP server, forming the core of the project, was kickstarted with an example from https://github.com/boostorg/beast/blob/9d05ef58d19672d91475e6be6f746d9bdb031362/example/http/server/small/http_server_small.cpp, which is licensed under the Boost license. Therefore it is easiest to license this project under the Boost license as well.

Copyright (c) 2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
Copyright (c) 2024 Matěj Volf

Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

The files `vendor/ada.*` are of the ada-url project, see https://github.com/ada-url/ada for licensing information.
