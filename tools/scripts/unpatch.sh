cwd=$1

for d in $(ls ${cwd}/patches); do
  if [ ! -d "${cwd}/${d}" ]; then
    continue
  fi
  echo "unpatch ${d}..."
  pushd ${cwd}/${d}
    git reset --hard
  popd
done
