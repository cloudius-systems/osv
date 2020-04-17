# Docker OSv builder
Docker files intended to help setup OSv build environment.
There are two versions of it - one based on Ubuntu and another on Fedora.

Build container image (default, based on Fedora)
```
docker build -t osv/builder -f Dockerfile.builder .
```

Build container image for specific version of linux distribution and git repo owner (if forker)
```
docker build -t osv/builder-ubuntu-19.10 -f Dockerfile.builder --build-arg DIST="ubuntu-19.10" --build-arg GIT_ORG_OR_USER=a_user .
```

Run container
```
docker run -it --privileged osv/builder
```

After starting you will end up in /git-repos/osv directory
where you can build OSv kernel and example app like so:
```bash
./scripts/build image=native-example
```

To run it:
```bash
./scripts/run.py
```

Run this to learn all kinds of options the build and run.py script accepts:
```bash
./scripts/build --help
./scripts/run.py --help
```

To refresh OSv code the latest version run this in /git-repos/osv directory:
```bash
git pull
```

To update Fedora/Ubuntu packages run this in /git-repos/osv directory:
```bash
./scripts/setup.py
```

# Docker OSv runner
Docker files intended to help setup OSv environment to run and test OSv.
There are two versions of it - one based on Ubuntu and another on Fedora.

Build container image
```
docker build -t osv/runner-ubuntu -f Dockerfile.runner-ubuntu . # Use specific docker file
```

Build container image for specific version of linux distribution and git repo owner (if forker)
```
docker build -t osv/runner-fedora-31 -f Dockerfile.runner-fedora --build-arg DIST_VERSION=31 --build-arg GIT_ORG_OR_USER=a_user .
```

After starting you will end up in /git-repos/osv directory
where you can build OSv image using capstan and run it using capstan or ./scripts/run.py
