cwd=$1
echo "MS_CPPARGS=[\"$(cat ${cwd}/mediasoup/worker/out/ms_cpparg.txt)\"]" > ${cwd}/../../tools/bazel/libs/ms_cppargs.bzl
