cwd=$1

for d in $(ls ${cwd}/patches); do
  if [ ! -d "${cwd}/${d}" ]; then
    continue
  fi
  echo "patching ${d}..."
  pushd ${cwd}/${d}
    git apply ${cwd}/patches/${d}/1.patch
  popd
done
