.PHONY: build
# debug/release
MODE=debug
# sanitize
SAN=address

# Set config based on MODE and SAN
CONFIG=--config=$(MODE)
ifneq ($(SAN),)
	CONFIG += --define=SAN=$(SAN)
endif

.PHONY: sys

sys:
	bazel query //...
	bazel build :server :client $(CONFIG) --verbose_failures

ext:
	make -C $(CURDIR)/sys/server/ext setup MODE=$(MODE) SAN=$(SAN)

all: ext sys

clean:
	bazel clean --expunge

erase: clean
	make -C $(CURDIR)/sys/server/ext clean
