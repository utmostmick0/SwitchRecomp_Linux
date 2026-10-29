# Descriptor-validated array sampler repair

Runtime: `v199-array-sampler-variants`. SHA-256: `39156582514fd4b3f0d656c23e99ee19f1661a0eaf24540ea7da012b9f06c38b`.

## Type correction

Section 3.8 identifies TIC type 5 as 1D array, but [NVIDIA's Maxwell definitions](https://github.com/NVIDIA/open-gpu-doc/blob/master/classes/3d/clb197tex.h) define type 4 as ONE_D_ARRAY and type 5 as TWO_D_ARRAY. The reported host target 0x8c1a is GL_TEXTURE_2D_ARRAY. Emitting sampler1DArray for this resource would be incorrect. No native 1D-array conversion was introduced.

## Implementation

[Shader translation](runtime/shader.c) and [fragment variant selection](runtime/gpu_gl.c) now specialize compatible non-array TEX/TLD uses to sampler2DArray when the live TIC is type 5, the resolved host target is GL_TEXTURE_2D_ARRAY, and it has exactly one layer. Sampling coordinates retain x/y and append layer zero (vec3 or ivec3 for texelFetch). Real array instructions keep their original layer operand. Gather, shadow, offsets, incompatible shared uses and multi-layer descriptors are excluded from this override.

Original opcode array metadata is tracked separately from the generated declaration, preserving stable slot IDs. Both shader caches compare the full array mask explicitly as well as hashing it into variant keys. The translator preserves the mask across initialization. Existing 2D and 3D variants remain distinct. Signed/unsigned sampler declarations retain their numeric type. The GL texture object and its uploaded storage are not relabeled.

## Validation

- Sanitized translator checks passed for 36 combinations of packed/ordinary TEX/TLD, base-2D/3D/2D-array layout, and floating/unsigned/signed samplers. Generated fragment sources passed glslangValidator. Repeated slot identity and incompatible-use checks remain.
- Linux X11 runtime build passed; staged and hash-verified for 10 launchers. Backups use `.pre-array-sampler`.
- Isolated 45-second TotK Vulkan-output startup run: 1,583 frames, 1,091,055 draws at 44.76 seconds; last interval 45.4 FPS. No recorded fatal error or shader compilation failure. The prior `TIC type 5 ... wants de1, has 8c1a` warning did not recur. The mismatch counter remains 32, with a separate missing-data 3D LUT fallback reported; zero overall texture errors is not claimed.
- This startup sample is not a controlled FPS comparison or proof of complete gameplay/visual correctness. The requested blanket conversion of type 5 to sampler1DArray was deliberately not applied.

[TotK log](games/runtime-array-sampler-20261029/totk/patched.runtime.log), [thread dump](games/runtime-array-sampler-20261029/totk/patched.threads.txt), [run metadata](games/runtime-array-sampler-20261029/runs.json).
