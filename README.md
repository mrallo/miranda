# Miranda&#8482; System
[![Docker](https://github.com/mrallo/miranda/actions/workflows/deploy-image.yml/badge.svg?branch=main)](https://github.com/mrallo/miranda/actions/workflows/deploy-image.yml)
[![CI](https://github.com/mrallo/miranda/actions/workflows/build-ci.yml/badge.svg)](https://github.com/mrallo/miranda/actions/workflows/build-ci.yml)

This directory contains everything you should need to create a working
version of the Miranda system.

## Instructions

Before compiling Miranda on a new host, execute the following commands:

```bash
make cleanup
```

This removes old object files and collects information about the
current platform in `.host`.

Then, execute:

```bash
make
```

This should recreate a working version of Miranda, in this
directory. To try it out, execute `./mira`.

Before doing the `make`, you might want to inspect the first few lines
of `Makefile` which sets the options to `cc` and a few other things
that might need adjusting.

There is a selection of example Miranda scripts in the directory
`ex`. For stress testing the garbage collector, try `./mira
ex/parafs.m` (say output).

Note that in a mira session, `/e` opens the editor (default `vi`) on
the current script.

Other makefile targets supported are (need to be executed as root):

- `make install`: copies the mira executables and associated files
(miralib, mira.1) to the appropriate places in the root filing system,
so they can be accessed by all users.
- `make release`: creates a gzipped tar image of the binaries suitable
for installing Miranda on other machines of the same object code type. 

To use the tar image on another machine, be root, and say:

```bash
cd /
tar xzpf [pathname]
```

where `[pathname]` is the gzipped tarfile.

Before `make install` or `make release`, you should inspect paths
`BIN`, `LIB`, `MAN` at the top of the `Makefile` and modify if needed,
to put things at the places in the root filing system where you want
them to go.

## Troubleshooting

Be aware that the garbage collector works by scanning the C stack to
find anything that is or seems to be a pointer into the heap (see
`bases()` in `data.c`) and is therefore somewhat fragile as it can be
foxed by aggressive compiler optimisations. GC errors manifest as
"impossible event" messages, or segmentation faults. If these appear,
try recompiling at a lower level of optimisation (e.g. without `-O`)
or with a different C compiler - e.g. `clang` instead of `gcc` or vice
versa.

## What to do if things need changing

It is possible that everything will work first time, just on saying
`make`. If however you are obliged to make changes for the new host
(the XYZ machine say), it best to proceed as follows.

The second line of the `Makefile` defines some `CFLAGS` (used by
`cc`). As delivered, most of these are commented out, leaving `-O` as
the only flag. Add a flag `-DXYZ` to the `CFLAGS` line. Then at each
place in a source file where you have to change something, do it in
the following style:

```c
#ifdef XYZ
    your modified code
#else
    the original code
#endif
```

You will see that this method has been used to cater for certain
machine dependencies at a few places in the sources. Looking to see
where this has already been done is likely to give you an idea as to
which lines may need modifying for your machine.

If you are running under System 5 UNIX, you may include `-DSYSTEM5` in
the `CFLAGS` line of the `Makefile`, as a couple of system 5
dependencies are `#ifdef`'d in the sources (relate to `signal()`,
unclear if they are still needed).

One other place where platform dependency is possible is in `twidth()`
near bottom of file `steer.c`, which uses an `ioctl()` call to find
width of current window. This feature isn't critical, however, just
aesthetic.

The sources have no documentation other than embedded comments: you
have to figure out how things work from these and the `Makefile`.

Reports of problems encountered, changes needed, etc to `mira-bugs`
(at) `miranda.org.uk`. Thanks!

David Turner
University of Kent
13.01.2020
