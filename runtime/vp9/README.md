# VP9 decoder

The Linux runtime uses libvpx 1.14.1 for VP9 WebM playback through `vp9video.inc`. The implementation is adapted from [Metroid Dread Recomp](https://github.com/DaniSald/Metroid-Dread-Recomp/tree/main/runtime), licensed under AGPL-3.0 like this project. libvpx is separately licensed under BSD-3-Clause; see `LICENSE-libvpx.txt`.

Linux uses the prebuilt static archive in `lin/` so installed runtimes do not need a separate libvpx package. The matching public headers are under `include/vpx/`.
