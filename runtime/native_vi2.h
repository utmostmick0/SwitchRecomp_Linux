/* SwitchRecomp, AGPL-3.0. Native VI2 image lifetime; guest ABI adaptation is separate. */
#ifndef SWR_NATIVE_VI2_H
#define SWR_NATIVE_VI2_H
#include "native_gpu.h"
typedef struct NativeVi2 NativeVi2;
NativeVi2* native_vi2_create(u64 owner);
void native_vi2_ref(NativeVi2*);
void native_vi2_unref(NativeVi2*);
/* Initial reviewed host surface: tightly bounded linear RGBA8, not a guessed
   block-linear NVN descriptor. Registration holds a native mapping lease. */
int native_vi2_register_rgba(NativeVi2*,u64 gpu_address,u32 width,u32 height,u32 pitch,u32* image);
int native_vi2_unregister(NativeVi2*,u32 image);
int native_vi2_acquire(NativeVi2*,u32 image,u64* ticket);
int native_vi2_cancel(NativeVi2*,u32 image,u64 ticket);
/* Optional real release semaphore; a pending fence returns NG_BUSY. */
int native_vi2_present(NativeVi2*,u32 image,u64 ticket,u64 fence,u32 threshold);
u64 native_vi2_completed_frames(NativeVi2*);
#endif
