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

all:
	bazel build :server :client $(CONFIG)

setup:
	make -C $(CURDIR)/sys/server/ext setup MODE=$(MODE) SAN=$(SAN)

build: setup all

clean:
	make -C $(CURDIR)/sys/server/ext clean
	bazel clean --expunge
