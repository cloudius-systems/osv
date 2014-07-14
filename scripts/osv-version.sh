#!/bin/sh

GITDIR=$(dirname $0)/../.git
git --git-dir $GITDIR describe --tags --match 'v[0-9]*'
