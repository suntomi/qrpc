MODE=debug
SAN=address
ifeq ($(MODE),debug)
	MS_PROFILE=Debug
	ifneq ($(SAN),)
		# please do make clean if you change the options
		# MESON_ARGS="-Db_sanitize=$(SAN) -Dms_rtc_logger_rtp=true"
		# if you need PIC objects of mediasoup:
		# -Db_pie=true -Db_staticpic=true
		MESON_ARGS="-Db_sanitize=$(SAN)"
	else
		MESON_ARGS=
	endif
else
	MS_PROFILE=Release
	MESON_ARGS=
endif

setup:
	if [ ! -f $(CURDIR)/mediasoup/worker/out/$(MS_PROFILE)/MESON_ARGS ] || \
		[ "$(MESON_ARGS)" != "$(shell cat $(CURDIR)/mediasoup/worker/out/$(MS_PROFILE)/MESON_ARGS)" ]; then \
		echo "remove mediasoup previous build cache by changing MESON_ARGS=$(MESON_ARGS)"; \
		make -C $(CURDIR)/mediasoup/worker clean-all; \
	fi
	make -C $(CURDIR)/mediasoup/worker libmediasoup-worker MEDIASOUP_BUILDTYPE=$(MS_PROFILE) MESON_ARGS=$(MESON_ARGS)
	echo "remove bazel project files to force copy all subprojects files into bazel sandbox"
	find $(CURDIR)/mediasoup/worker/subprojects -name "BUILD*" -type f -delete
	echo "copy flatbuffer generated header files to path"
	(rm -rf $(CURDIR)/mediasoup/worker/include/FBS || true) && \
		cp -r $(CURDIR)/mediasoup/worker/out/$(MS_PROFILE)/build/fbs/FBS $(CURDIR)/mediasoup/worker/include/FBS
	echo "export mediasoup cpparg info"
	bash $(CURDIR)/../../tools/scripts/gen_mscppargs.sh $(CURDIR)
	echo "$(MESON_ARGS)" > $(CURDIR)/mediasoup/worker/out/$(MS_PROFILE)/MESON_ARGS

clean:
	make -C $(CURDIR)/mediasoup/worker clean-all
