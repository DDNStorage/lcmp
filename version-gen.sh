#!/bin/sh

VERSION="0.1"
if [ -d .git ]; then
	VERSION=${VERSION}".g$(git rev-parse --short HEAD)"
fi

printf "%s" "$VERSION"
