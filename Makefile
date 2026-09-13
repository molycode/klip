# A thin wrapper over CMake, for building Klip without learning its presets.
# Everything here is a convenience: `cmake -S . -B build && cmake --build build` does the same work.
#
#   make                 build (Release)
#   make run             build, then run it from the build tree
#   make install         build, then install to $(PREFIX)
#   make uninstall       remove the files that install wrote
#   make debug           build with assertions and no optimisation
#   make clean           remove this build type's directory
#
# Filing a bug? Build with BUILD_TYPE=RelWithDebInfo: Release compiles logging out and carries no
# symbols, so a crash there gives an address and nothing to read beside it.

PREFIX     ?= /usr/local
BUILD_TYPE ?= Release
BUILD_DIR  ?= build/$(BUILD_TYPE)

# Ninja when it is there, because it is what the presets use; plain make otherwise, so the absence of
# Ninja is not a wall for someone who only wants the binary.
GENERATOR := $(shell command -v ninja >/dev/null 2>&1 && echo Ninja || echo "Unix Makefiles")

.PHONY: all build install uninstall run debug clean

all: build

$(BUILD_DIR)/CMakeCache.txt:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(PREFIX)

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --parallel
	@echo
	@echo "built $(BUILD_DIR)/src/app/klip"

install: build
	cmake --install $(BUILD_DIR) --prefix $(PREFIX)

# Not a dependency of build: there is nothing to compile in order to delete what is already installed.
uninstall:
	cmake --build $(BUILD_DIR) --target uninstall

run: build
	$(BUILD_DIR)/src/app/klip

debug:
	$(MAKE) BUILD_TYPE=Debug build

clean:
	rm -rf $(BUILD_DIR)
