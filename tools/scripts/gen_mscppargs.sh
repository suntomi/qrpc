cwd=$1
echo "MS_CPPARGS=[\"$(cat ${cwd}/mediasoup/worker/out/ms_cpparg.txt | sed -e "s/:/\",\"/g")\"]" > ${cwd}/../../../tools/bazel/libs/ms_cppargs.bzl
