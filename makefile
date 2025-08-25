.PHONY: build
# debug/release
MODE ?= debug
# architecture
PLATFORM ?= darwin_arm64
# sanitize
ifeq ($(MODE),debug)
	ifeq ($(PLATFORM),darwin_arm64)
		SAN ?= address
	else
# for non-darwin platform, sanitizer is off by default
# even if it is set to debug mode
# because asan seems to be too slow on linux platform
# should use valgrind instead
		SAN ?= none
	endif
	GDB ?= gdb
else
	SAN ?= none
	GDB ?= 
endif
# build options
BUILD_OPT = --config=$(MODE) 
ifneq ($(SAN),none)
	BUILD_OPT += --define=SAN=$(SAN)
endif
ifeq ($(PLATFORM),linux_arm64)
	BUILD_OPT += --cpu=aarch64 --nostart_end_lib
else ifeq ($(PLATFORM),linux_amd64)
	BUILD_OPT += --cpu=x86_64 --nostart_end_lib
else
	BUILD_OPT += --cpu=$(PLATFORM)
endif
# build target
TARGET ?= e2e

.PHONY: lib

lib:
	bazel build :server :client :lib $(BUILD_OPT)

ext:
	make -C $(CURDIR)/lib/ext setup MODE=$(MODE) SAN=$(SAN)

all: ext lib

clean:
	bazel clean --expunge

erase: clean
	make -C $(CURDIR)/lib/ext clean

rundev:
	docker run --rm -ti -p 8888:8888/tcp -p 11111:11111/udp \
		-e QRPC_E2E_SFU_IP=192.168.64.1 -e QRPC_E2E_SECURE=1 \
		--name e2e suntomi/qrpc:e2e $(GDB) ./e2e_server

image:
	MODE=$(MODE) SAN=$(SAN) bash $(CURDIR)/deploy/scripts/image/build.sh $(TARGET)