.PHONY: build
# debug/release
MODE=debug
# sanitize
SAN=address
# bazel build opt: -s to show build command
ifeq ($(MODE),debug)
	ifneq ($(SAN),)
		BUILD_OPT=--compilation_mode=dbg -s --define=SAN=$(SAN)
	else
		BUILD_OPT=--compilation_mode=dbg -s
	endif
else
	BUILD_OPT=--compilation_mode=opt -s
endif

all:
	bazel build :server :client $(BUILD_OPT) --cpu=darwin_arm64 --features=oso_prefix_is_pwd

setup:
	make -C $(CURDIR)/sys/server/ext setup MODE=$(MODE) SAN=$(SAN)

build: setup all

clean:
	make -C $(CURDIR)/sys/server/ext clean
	bazel clean --expunge
