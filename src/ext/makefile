MODE=debug
SAN=address
ifeq ($(MODE),debug)
	MS_PROFILE=Debug
	ifneq ($(SAN),)
		MESON_ARGS="-Db_sanitize=$(SAN)"
	else
		MESON_ARGS=
	endif
else
	MS_PROFILE=Release
	MESON_ARGS=
endif

setup:
	make -C $(CURDIR)/mediasoup/worker libmediasoup-worker MEDIASOUP_BUILDTYPE=$(MS_PROFILE) MESON_ARGS=$(MESON_ARGS)
	echo "remove bazel project files to force copy all subprojects files into bazel sandbox"
	find $(CURDIR)/mediasoup/worker/subprojects -name "BUILD*" -type f -delete
	echo "copy flatbuffer generated header files to path"
	(rm -rf $(CURDIR)/mediasoup/worker/include/FBS || true) && \
		cp -r $(CURDIR)/mediasoup/worker/out/$(MS_PROFILE)/build/fbs/FBS $(CURDIR)/mediasoup/worker/include/FBS
	echo "export mediasoup cpparg info"
	bash $(CURDIR)/../../tools/scripts/gen_mscppargs.sh $(CURDIR)
	echo "configure openssl to generate configure.h"
	cd $(CURDIR)/mediasoup/worker/subprojects/openssl-3.0.8 && ./config
	echo "build openssl mandatory generated files"
	make -C $(CURDIR)/mediasoup/worker/subprojects/openssl-3.0.8 build_generated

clean:
	make -C $(CURDIR)/mediasoup/worker clean-all
