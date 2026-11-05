/* SwitchRecomp, AGPL-3.0. C boundary for the imported Eden Ampere frontend. */
#ifndef SWR_NS2_SHADER_H
#define SWR_NS2_SHADER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  uint8_t* code;             /* malloc'd Maxwell image; caller frees it */
  uint32_t size;
  uint32_t consumed;         /* native bytes, including the selected header */
  uint32_t error_offset;
  char error[160];
} SwrNs2Shader;
/* Input is an explicitly selected ISA, never detected from arbitrary words.
   Graphics carries a 0x80-byte header; compute is headerless. The input may
   contain allocation padding after a control-flow-complete program. */
int swr_ns2_shader_lower(const uint8_t* source, uint32_t size, int compute,
                         SwrNs2Shader* result);
typedef int (*SwrNs2ShaderRead)(void*, uint64_t, void*, uint32_t);
/* The reader must reject unmapped ranges. Each instruction is read separately. */
int swr_ns2_shader_capture(uint64_t address, int compute, SwrNs2ShaderRead read,
                           void* context, SwrNs2Shader* result);
#ifdef __cplusplus
}
#endif
#endif
