.PHONY: sys
# debug/release
MODE=debug
# sanitize
ifeq ($(MODE),debug)
	SAN=address
else
	SAN=
endif

# Set config based on MODE and SAN
CONFIG=--config=$(MODE)
ifneq ($(SAN),)
	CONFIG += --define=SAN=$(SAN)
endif

sys:
	bazel build :server :client $(CONFIG)

ext:
	make -C $(CURDIR)/sys/server/ext setup MODE=$(MODE) SAN=$(SAN)

all: ext sys

clean:
	bazel clean --expunge

clean-all: clean
	make -C $(CURDIR)/sys/server/ext clean MODE=$(MODE) SAN=$(SAN)
