#!/bin/sh
for pkg in "$@"
do
  VERSION=$(apt show $pkg 2> /dev/null | grep -F "Version: " | sed -e 's/Version:\s\([[[:digit:]\.]\+\).*/\1/')
  if [ $pkg != $1 ]; then
    echo -n ", "
  fi
  echo -n "$pkg (>= $VERSION)"
done
