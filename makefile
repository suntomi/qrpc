.PHONY: build

OS=osx
ARCH=arm64

all:
	bazel build :server --sandbox_debug --define os=$(OS)

build:
	make -C $(CURDIR)/src/ext setup
	bazel build :server --sandbox_debug

clean:
	make -C $(CURDIR)/src/ext clean
	bazel clean --expunge
