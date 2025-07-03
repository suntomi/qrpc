.PHONY: build
# debug/release
MODE=debug
# sanitize
SAN=address
# architecture
PLATFORM=darwin_arm64
# build options
BUILD_OPT=--config=$(MODE) 
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
	bazel build -s :server :client :lib $(BUILD_OPT)

ext:
	make -C $(CURDIR)/sys/server/ext setup MODE=$(MODE) SAN=$(SAN)

all: ext sys

clean:
	bazel clean --expunge

erase: clean
	make -C $(CURDIR)/sys/server/ext clean
