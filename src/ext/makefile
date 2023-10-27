setup:
	make -C $(CURDIR)/mediasoup/worker libmediasoup-worker
	echo "remove bazel project files to force copy all subprojects files into bazel sandbox"
	find $(CURDIR)/mediasoup/worker/subprojects -name "BUILD*" -type f -delete

clean:
	make -C $(CURDIR)/mediasoup/worker clean-all
	