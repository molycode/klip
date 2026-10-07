#!/usr/bin/env bash
# Builds Klip at its declared floor: Ubuntu 24.04's FFmpeg and other libraries with the oldest tools C++26
# allows -- GCC 14, Clang 19 and CMake 3.30 -- inside a container, from a copy of this tree without
# CMakeUserPresets.json, and runs the test suite in the Debug builds. The packages are read from the README's
# apt line, so a dependency missing from it fails here rather than on a reader's machine.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
image="klip-floor:ubuntu-24.04"
cmake_version="3.30.9"
cmake_sha256="9114e33358a9efc93d6ea658805280fc3201b882b944a4d946edd9472fd1eec7"
cmake_url="https://github.com/Kitware/CMake/releases/download/v$cmake_version/cmake-$cmake_version-linux-x86_64.tar.gz"

packages="$(sed -n '/sudo apt install/,/[^\\]$/p' "$root/README.md" | sed 's/sudo apt install//; s/\\//g' | xargs)"

if [ -z "$packages" ]; then
	echo "no 'sudo apt install' line found in README.md" >&2
	exit 1
fi

docker build --quiet --tag "$image" - > /dev/null <<EOF
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y $packages g++-14 clang-19 dbus-daemon \
	curl ca-certificates && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL -o /tmp/cmake.tar.gz $cmake_url && echo "$cmake_sha256  /tmp/cmake.tar.gz" | sha256sum -c - \
	&& tar -C /opt -xzf /tmp/cmake.tar.gz && rm /tmp/cmake.tar.gz
ENV PATH=/opt/cmake-$cmake_version-linux-x86_64/bin:\$PATH
EOF

tar -C "$root" --exclude=./build --exclude=./.git --exclude=./CMakeUserPresets.json -cf - . |
	docker run --rm -i "$image" bash -c '
		set -uo pipefail
		mkdir /src && tar -C /src -xf -
		echo "cmake $(cmake --version | head -1 | cut -d" " -f3), $(g++-14 --version | head -1), $(clang++-19 --version | head -1)"

		failed=0

		build() {
			local name="$1"
			shift
			printf "%-16s " "$name"

			if cmake -S /src -B "/b/$name" -G Ninja "$@" > "/b/$name.cfg" 2>&1 \
				&& cmake --build "/b/$name" > "/b/$name.log" 2>&1 \
				&& { ! grep -q "KLIP_BUILD_TESTS:BOOL=ON" "/b/$name/CMakeCache.txt" \
					|| ctest --test-dir "/b/$name" --output-on-failure >> "/b/$name.log" 2>&1; }; then
				echo "OK (warnings: $(grep -c "warning:" "/b/$name.log"))$(grep -o "[0-9]*% tests passed" "/b/$name.log" | sed "s/^/, /")"
			else
				echo FAILED
				grep -m1 -A8 -E "error|Error" "/b/$name.log" "/b/$name.cfg" 2>/dev/null | head -12
				failed=1
			fi
		}

		mkdir -p /b

		# What the README tells a reader to type.
		printf "%-16s " "make"

		if make -C /src CXX=g++-14 CC=gcc-14 > /b/make.log 2>&1; then
			echo "OK (warnings: $(grep -c "warning:" /b/make.log))"
		else
			echo FAILED
			grep -m1 -A8 -E "error|Error" /b/make.log | head -12
			failed=1
		fi

		for type in Debug Release; do
			build "gcc14-$type" -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/linux/gcc.cmake \
				-DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_BUILD_TYPE=$type \
				-DKLIP_BUILD_TESTS=$([ $type = Debug ] && echo ON || echo OFF)
			build "clang19-$type" -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/linux/clang.cmake \
				-DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=$type \
				-DKLIP_BUILD_TESTS=$([ $type = Debug ] && echo ON || echo OFF)
		done

		exit $failed
	'
