/* Native software DSP adapter. See LICENSES/primerecomp-MIT.txt. */
#ifndef SWR_AUDIO_EFFECTS_H
#define SWR_AUDIO_EFFECTS_H
#include <stdint.h>
typedef struct SwrDspEffect SwrDspEffect;
SwrDspEffect* swr_dsp_configure(SwrDspEffect* effect,const uint8_t config[0xc0]);
void swr_dsp_free(SwrDspEffect* effect);
void swr_dsp_process(SwrDspEffect* effect,float* mix,unsigned buffers,unsigned frames);
#endif
