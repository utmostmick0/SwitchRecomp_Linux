# Linux OpenH264 archive

`libopenh264.a` is the Linux x86_64 static OpenH264 library built from Cisco's upstream `v2.4.1` tag
(commit `c59550a2147c255cc8e09451f6deb96de2526b6d`). The vendored API headers in
`../include` are also version 2.4.1.

Rebuild on Linux x86_64 with:

```sh
git clone --depth 1 --branch v2.4.1 https://github.com/cisco/openh264.git
make -C openh264 -j2 V=No libopenh264.a
cp openh264/libopenh264.a runtime/oh264/lin/libopenh264.a
```

The upstream BSD license is in `../LICENSE`. This archive is linked statically into the Linux runtime.
