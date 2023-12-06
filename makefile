.PHONY: build
BUILD := Debugs

# debug/release
MODE=debug
ifeq ($(MODE),debug)
	BUILD_OPT=--compilation_mode=dbg -s
else
	BUILD_OPT=--compilation_mode=opt -s
endif

all:
	bazel build :server $(BUILD_OPT) --cpu=darwin_arm64 --features=oso_prefix_is_pwd

setup:
	make -C $(CURDIR)/src/ext setup MODE=$(MODE)

build: setup all

clean:
	make -C $(CURDIR)/src/ext clean
	bazel clean --expunge
