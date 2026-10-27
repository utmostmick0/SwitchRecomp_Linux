# Linux VP9 decoder

`libvpx.a` is the static libvpx 1.14.1 decoder library (WebM Project BSD-3-Clause), built for x86-64 Linux from upstream commit `12f3a2ac603e8f10742105519e0cd03c3b8f71dd` with VP9 decoding enabled and encoders, command-line tools, examples, and tests disabled. Decoder multithreading is enabled. See `../LICENSE-libvpx.txt`.

The public headers in `../include/vpx/` are from the same upstream revision. `switchrecomp.py --build-runtime` links this archive when present; without it the runtime still builds but VP9 movies remain black.
