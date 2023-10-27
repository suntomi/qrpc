.PHONY: build

all:
	bazel build :server --sandbox_debug

build:
	make -C $(CURDIR)/src/ext setup
	bazel build :server --sandbox_debug

clean:
	make -C $(CURDIR)/src/ext clean
	bazel clean --expunge
