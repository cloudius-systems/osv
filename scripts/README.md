This directory contains number of scripts aimed to help **build, run and test** OSv images. 

**DISCLAIMER**: Please note that some of the scripts were written years ago and are no longer maintained and potentially obsolete. 
Please look at the creation date as a good indicator. When in doubt on how to use any of them post a question on [OSV mailing list](https://groups.google.com/forum/#!forum/osv-dev).

# Setup
* **setup.py** - Python script that detects the type of Linux distribution and installs all necessary packages to build and run OSv; currently following Linux distributions are supported:
  * Debian
  * Fedora
  * Ubuntu
  * Linux Mint
  * RHEL

Please note that only Fedora and Ubuntu are actively maintained and tested. Linux Mint was alre recenty added so there is good chance it might be up to date.  

# Building OSv
* **build** - main shell script that orchestrates building OSv images by delegating to the main [Makefile](https://github.com/cloudius-systems/osv/blob/master/Makefile) 
  to build kernel and number of Python scripts like [module.py](https://github.com/cloudius-systems/osv/blob/master/scripts/module.py) to 
  build application and fuse together into a final image ```./build/$(arch)/usr.img```; for details how to use run ```./scripts/build --help```
* **module.py** - Python script that iterates over all passed in modules and/or apps to the main build, resolves any dependant ones, 
  invokes their make files and concatenates final build manifest (```./build/$(arch)/usr.manifest``` - list of OSv/host path pairs)
* **manifest_common.py** - 
* **upload_manifest.py** - 
* **gen-rofs-img.py** - 
* **mkbootfs.py** - 
* **imgedit.py** - 
* **export_manifest.py** - 
* **nbd_client.py** - 

# Running OSv
* **run.py** - main script intended to run the OSv image built using scripts/build; please run ```./script/run.py --help``` for details
* **firecracker.py** - version of the run script intended to run the OSv image on firecracker; please run ```./script/firecracker.py --help``` for details

# Testing OSv
* **test.py** - main script intended to orchestrate running the OSv unit tests; invoked by build when run like so: ```./scripts/build check```; 
  this script delegates to other Python scripts under scripts/tests
* **loader.py**  - used by gdb to assist with debugging OSv (for details look at this [Wiki page](https://github.com/cloudius-systems/osv/wiki/Debugging-OSv))
