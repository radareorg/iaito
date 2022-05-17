#!/bin/sh

r2 -v >/dev/null 2>&1
if [ $? -eq 0 ]; then
  echo "Correct radare2 version found, skipping..."
  exit 0
fi

printf "A (new?) version of radare2 will be installed. Do you agree? [Y/n] "
read -r answer
case "$answer" in
""|y|Y)
  R2PREFIX=${1:-"/usr"}
  git submodule init && git submodule update
  cd radare2 || exit 1
  ./sys/install.sh "$R2PREFIX"
  cd ..
  ;;
*)
  echo "Sorry but you can't move forward without r2 installed"
  exit 1
esac
