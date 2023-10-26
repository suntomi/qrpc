CWD=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)

curl -L https://raw.githubusercontent.com/tensorflow/tensorflow/master/.bazelrc -o ${CWD}/cross.bazel.rc