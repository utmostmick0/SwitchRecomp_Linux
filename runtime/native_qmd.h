/* SwitchRecomp, AGPL-3.0. Process-owned QMD3 execution boundary. */
#ifndef SWR_NATIVE_QMD_H
#define SWR_NATIVE_QMD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SwrNativeQmd SwrNativeQmd;
typedef int (*SwrNativeRead)(void*,uint64_t,void*,uint32_t);
typedef struct {
  uint64_t program;
  uint32_t grid[3],block[3],shared_bytes,local_pos_bytes,local_crs_bytes,linked_tsc;
  struct { uint64_t address; uint32_t size,enabled; } cbuf[8];
} SwrNativeLaunch;
typedef struct {
  /* acquire pins an owned mapped range; token is unique until release. */
  int (*acquire)(void*,uint64_t,uint64_t,uint32_t*);
  void (*release)(void*,uint32_t);
  SwrNativeRead read;
  int (*write_release)(void*,uint64_t,uint32_t,uint32_t);
  /* Success means GPU execution AND output visibility have completed. An
     asynchronously queued command is not a successful synchronous dispatch. */
  int (*dispatch)(void*,const SwrNativeLaunch*,SwrNativeRead,void*);
} SwrNativeQmdBackend;
SwrNativeQmd* swr_native_qmd_create(const SwrNativeQmdBackend*,void* owner);
void swr_native_qmd_destroy(SwrNativeQmd*);
int swr_native_qmd_signal(SwrNativeQmd*,uint64_t address,uint32_t action,char* error,uint32_t error_size);
#ifdef __cplusplus
}
#endif
#endif
