# Docker OSv builder
Docker files intended to help setup OSv build environment.
There are two versions of it - one based on Ubuntu and another on Fedora.

Build container image
```
docker build -t osv/builder .                      # Use default Fedora-base file 
docker build -t osv/builder -f Dockerfile.Ubuntu . # Use specific docker file
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
./script/setup.py
```
