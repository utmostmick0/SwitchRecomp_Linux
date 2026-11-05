/* SwitchRecomp, AGPL-3.0. Native GPU resource ownership, independent of IPC. */
#ifndef SWR_NATIVE_GPU_H
#define SWR_NATIVE_GPU_H
#include "cpu.h"
enum { NG_OK, NG_INVALID, NG_NOT_FOUND, NG_BUSY, NG_LIMIT, NG_UNMAPPED, NG_UNSUPPORTED };
/* Owners are minted by the runtime, never taken from a guest packet. */
u64 nv_native_owner_open(void);
int nv_native_owner_close(u64 owner);
int nv_native_owner_retain(u64 owner);
int nv_native_owner_drop(u64 owner); /* drops a dependent reference, not the root */
int nv_native_memory_register(u64 owner,u64 address,u64 size,u32* memory);
int nv_native_memory_retain(u64 owner,u32 memory);
int nv_native_memory_release(u64 owner,u32 memory);
int nv_native_memory_info(u64 owner,u32 memory,u64* address,u64* size);
int nv_native_map(u64 owner,u32 memory,u64 gpu_address,u64 offset,u64 size);
int nv_native_unmap(u64 owner,u64 gpu_address,u64 size);
/* A unique lease blocks unmapping and physical retirement until completion. */
int nv_native_range_acquire(u64 owner,u64 gpu_address,u64 size,u32* lease);
int nv_native_range_release(u64 owner,u32 lease);
int nv_native_range_address(u64 owner,u64 gpu_address,u64 size,u64* cpu_address);

struct GpuChannel;
typedef struct NativeGpuWorker NativeGpuWorker;
NativeGpuWorker* native_worker_create(u64 owner);
void native_worker_ref(NativeGpuWorker*);
void native_worker_unref(NativeGpuWorker*);
/* Success means queue admission only. QMD releases denote real completion. */
int native_worker_submit(NativeGpuWorker*,struct GpuChannel*,u64 qmd,u32 action);
/* GPU-thread entry, invoked by the work queue while its owner reference lives. */
void native_worker_execute(NativeGpuWorker*,struct GpuChannel*,u64 qmd,u32 action,u32 lease);
#endif
