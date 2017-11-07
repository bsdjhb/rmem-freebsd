# rmem-freebsd

This repository contains sample tests for the 'rmem' modeling tool
derived from the FreeBSD kernel.

## include/

The include/ subdirectory holds headers from the FreeBSD kernel, mostly
unmodified, but in some cases modified to provide a simpler implementation.

## mtx_spin/

A simple test of FreeBSD's spin mutexes.  A binary for aarch64 can be built
with 'make' assuming that a Clang compiler is available.
