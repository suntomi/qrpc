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
else
	SAN ?= none
endif
# build options
BUILD_OPT = --config=$(MODE) 
ifneq ($(SAN),none)
	BUILD_OPT += --define=SAN=$(SAN)
endif
ifeq ($(PLATFORM),linux_arm64)
	BUILD_OPT += --cpu=aarch64
else ifeq ($(PLATFORM),linux_amd64)
	BUILD_OPT += --cpu=x86_64
else
	BUILD_OPT += --cpu=$(PLATFORM)
endif

.PHONY: sys

sys:
	bazel build :server :client :lib $(BUILD_OPT)

ext:
	make -C $(CURDIR)/sys/server/ext setup MODE=$(MODE) SAN=$(SAN)

all: ext sys

clean:
	bazel clean --expunge

erase: clean
	make -C $(CURDIR)/sys/server/ext clean
