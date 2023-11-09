cwd=$1

set -eo pipefail

for d in $(ls ${cwd}/patches); do
  if [ ! -d "${cwd}/${d}" ]; then
    continue
  fi
  echo "make patch for ${d}... > ${cwd}/${d}"
  pushd ${cwd}/${d}
    git diff --patch > ${cwd}/patches/${d}/1.patch
  popd
done
