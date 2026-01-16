SpeeDR – Dynamic Range Calculator
==================================

What this is
-------------

This is an implementation of the dynamic range (DR) metric specified by the
Pleasurize Music Foundation (PMF) and implemented by e.g. [MAAT DROffline MkII][]
or [DR14 T.meter][], but significantly faster than those (hence the name).

[MAAT DROffline MkII]: https://www.maat.digital/dro2/
[DR14 T.meter]: https://github.com/simon-r/dr14_t.meter

This project is not affiliated with the Pleasurize Music Foundation, nor with
either of the aforementioned implementations.

How to build and use
---------------------

### Building

Make sure to have a C++ compiler, [meson][] and [libsndfile][] installed (and
optionally OpenMP for file-level parallelism), create an empty build directory
somewhere, switch to it and run:

[meson]: https://mesonbuild.com/
[libsndfile]: https://libsndfile.github.io/libsndfile/

```console
$ meson setup -Dbuildtype=release .../speedr/  # adjust path to source as necessary
$ ninja
```

### Running

With the command-line executable now in your hands, running it is as
straightforward as:

```console
$ ./speedr /path/to/album/*.flac
```

It will display a DR rating for each input file, as well as an album rating if
passed several files at once.
