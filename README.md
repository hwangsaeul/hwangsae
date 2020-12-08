![Build Status](https://github.com/hwangsaeul/hwangsae/workflows/CI/badge.svg?branch=master)

# Hwangsae

*Hwangsae* is a library that implements forwarding and stream access control
services for Hwangsaeul.

For C API of *libhwangsae* see [relay.h](hwangsae/relay.h).

For relay service that makes use of *libhwangsae* see [Gaeul relay agent](https://github.com/hwangsaeul/gaeul/blob/master/README.md).

## PPA nightly builds

Experimental versions of Hwangsae are daily generated in [launchpad](https://launchpad.net/~hwangsaeul/+archive/ubuntu/nightly).

```console
$ sudo add-apt-repository ppa:hwangsaeul/nightly
$ sudo apt-get update
$ sudo apt-get install libhwangsae-dev libhwangsae2
```
