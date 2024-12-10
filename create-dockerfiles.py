template = """### Do not modify manually, generated by create-dockerfiles.py ###
FROM {image} AS builder

RUN {install_dev}

WORKDIR /app
COPY CMakeLists.txt .
COPY src src

WORKDIR /app/build
RUN cmake -DCMAKE_BUILD_TYPE=Release .. && make -j

FROM {image}

RUN {install_runtime}


WORKDIR /app
COPY --from=builder /app/build/asset-server .

ENTRYPOINT [ "./asset-server" ]
"""

platforms = [
    {
        "name": "debian",
        "image": "debian:bookworm",
        "install-command": "apt update && apt install {deps} --no-install-recommends -y  && rm -rf /var/lib/apt/lists/*",
        "dev-deps": [
            "libvips-dev",
            "libmagic-dev",
            "libboost-dev",
            "cmake",
            "make",
            "git",
            "ca-certificates",
        ],
        "runtime-deps": ["libvips", "libmagic1"],
    },
    {
        "name": "alpine",
        "image": "alpine:3",
        "install-command": "apk add --no-cache {deps}",
        "dev-deps": [
            "openssl-dev",
            "vips-dev",
            "boost-dev",
            "file-dev",
            "cmake",
            "make",
            "git",
        ],
        "runtime-deps": ["vips", "openssl", "libmagic"],
    },
    {
        "name": "fedora",
        "image": "fedora:41",
        "install-command": "dnf install --setopt=install_weak_deps=False -y {deps} && dnf clean all",
        "dev-deps": [
            "openssl-devel",
            "vips-devel",
            "boost-devel",
            "file-devel",
            "cmake",
            "make",
            "git",
        ],
        "runtime-deps": ["vips", "openssl"],
    },
]

compilers = [{"name": "gcc", "depname": "g++"}, {"name": "clang", "depname": "clang"}]

for platform in platforms:
    for compiler in compilers:
        with open(f"Dockerfile.{platform['name']}-{compiler['name']}", "w") as f:
            dev_deps = platform["dev-deps"].copy()
            dev_deps.append(compiler["depname"])
            f.write(
                template.format(
                    image=platform["image"],
                    install_dev=platform["install-command"].format(
                        deps=" ".join(dev_deps)
                    ),
                    install_runtime=platform["install-command"].format(
                        deps=" ".join(platform["runtime-deps"])
                    ),
                )
            )
