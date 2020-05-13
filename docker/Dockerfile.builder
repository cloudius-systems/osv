#
# Copyright (C) 2017 XLAB, Ltd.
# Copyright (C) 2018-2020 Waldemar Kozaczuk
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# This Docker file defines a container intended to build, test and publish
# OSv kernel as well as many applications ...
#
ARG DIST="fedora-31"
FROM osvunikernel/osv-${DIST}-builder-base

#
# PREPARE ENVIRONMENT
#

# - prepare directories
RUN mkdir -p /git-repos

# - clone OSv
WORKDIR /git-repos
ARG GIT_ORG_OR_USER=cloudius-systems
RUN git clone https://github.com/${GIT_ORG_OR_USER}/osv.git
WORKDIR /git-repos/osv
RUN git submodule update --init --recursive

# - update all required packages in case they have changed
RUN scripts/setup.py

CMD /bin/bash

#
# NOTES
#
# Build the container based on default Fedora 31 base image:
# docker build -t osv/builder-fedora-31 -f Dockerfile.builder .
#
# Build the container based of specific Ubuntu version
# docker build -t osv/builder-ubuntu-20.04 -f Dockerfile.builder --build-arg DIST="ubuntu-20.04" .
#
# Build the container based of specific Fedora version and git repo owner (if forked) example:
# docker build -t osv/builder-fedora-31 -f Dockerfile.builder --build-arg DIST="fedora-31" --build-arg GIT_ORG_OR_USER=a_user .
#
# Run the container FIRST time example:
# docker run -it --privileged osv/builder-fedora-31
#
# To restart:
# docker restart ID (from docker ps -a) && docker attach ID
#
# To open in another console
# docker exec -it ID /bin/bash
