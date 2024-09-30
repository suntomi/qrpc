MODE=debug
ifeq ($(MODE),debug)
	MS_PROFILE=Debug
else
	MS_PROFILE=Release
endif

setup:
	make -C $(CURDIR)/mediasoup/worker libmediasoup-worker MEDIASOUP_BUILDTYPE=$(MS_PROFILE)
	echo "remove bazel project files to force copy all subprojects files into bazel sandbox"
	find $(CURDIR)/mediasoup/worker/subprojects -name "BUILD*" -type f -delete
	echo "export mediasoup cpparg info"
	bash $(CURDIR)/../../tools/scripts/gen_mscppargs.sh $(CURDIR)
	echo "configure openssl to generate configure.h"
	cd $(CURDIR)/mediasoup/worker/subprojects/openssl-3.0.8 && ./config
	echo "build openssl mandatory generated files"
	make -C $(CURDIR)/mediasoup/worker/subprojects/openssl-3.0.8 build_generated

clean:
	make -C $(CURDIR)/mediasoup/worker clean-all
