# One-command entry points. Local targets need a C++20 compiler, CMake >= 3.24
# and Ninja; `make docker` and `make docker-test` only need Docker.

BUILD ?= build/release
GENERATOR ?= Ninja
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

.PHONY: all build test unit integration bench demo asan tsan format format-check lint py-test docker docker-test clean

all: test

build:
	cmake -S . -B $(BUILD) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j $(JOBS)

test: build
	ctest --test-dir $(BUILD) --output-on-failure -j $(JOBS)

unit: build
	ctest --test-dir $(BUILD) --output-on-failure -LE integration -j $(JOBS)

integration: build
	ctest --test-dir $(BUILD) --output-on-failure -L integration

bench: build
	$(BUILD)/bench/tickmatch_bench

demo: build
	REPLAY=$(BUILD)/apps/tickmatch_replay scripts/demo.sh

asan:
	cmake -S . -B build/asan -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Debug -DTICKMATCH_SANITIZE=address,undefined -DTICKMATCH_BUILD_BENCHMARKS=OFF
	cmake --build build/asan -j $(JOBS)
	ctest --test-dir build/asan --output-on-failure -j $(JOBS)

# Newer kernels use high mmap ASLR entropy that older ThreadSanitizer runtimes
# reject; setarch -R disables ASLR for the build (test discovery runs the
# binary) and the test run.
tsan:
	cmake -S . -B build/tsan -G $(GENERATOR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTICKMATCH_SANITIZE=thread -DTICKMATCH_BUILD_BENCHMARKS=OFF
	setarch $$(uname -m) -R cmake --build build/tsan -j $(JOBS)
	setarch $$(uname -m) -R ctest --test-dir build/tsan --output-on-failure -j $(JOBS)

CXX_SOURCES = $(shell git ls-files '*.cpp' '*.hpp')

format:
	clang-format -i $(CXX_SOURCES)
	cd client && ruff format .

format-check:
	clang-format --dry-run --Werror $(CXX_SOURCES)

lint: format-check
	cd client && ruff check . && ruff format --check .

py-test:
	cd client && python3 -m pytest -q

docker:
	mkdir -p out
	HOST_UID=$$(id -u) HOST_GID=$$(id -g) docker compose up --build demo

docker-test:
	docker build --target build -t tickmatch:build .

clean:
	rm -rf build out
