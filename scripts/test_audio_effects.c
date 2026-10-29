/* Standalone native DSP checks: no guest, device, SDL or frontend required. */
#include "../runtime/audio_effects.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t* p,uint16_t v){ memcpy(p,&v,2); }
static void put32(uint8_t* p,uint32_t v){ memcpy(p,&v,4); }
static void putf(uint8_t* p,float v){ memcpy(p,&v,4); }
static void base(uint8_t* c,unsigned type,unsigned ch){
    memset(c,0,0xc0); c[0]=type; c[2]=1;
    put16(c+0x20+12,ch); put16(c+0x20+14,ch);
    for(unsigned i=0;i<ch;i++){ c[0x20+i]=i; c[0x26+i]=i; }
}

int main(void){
    uint8_t config[0xc0]; float mix[6*240]={0};
    base(config,3,1); uint8_t* p=config+0x20;
    put32(p+16,10); put32(p+20,1); put32(p+24,48000u<<14);
    put32(p+28,1<<14); put32(p+36,1<<14);
    SwrDspEffect* effect=swr_dsp_configure(NULL,config); assert(effect);
    mix[0]=1; swr_dsp_process(effect,mix,1,240);
    for(int i=0;i<240;i++) assert(fabsf(mix[i]-(i==48?1.f:0.f))<1e-6f);
    /* History survives a block boundary and an unchanged parameter update. */
    memset(mix,0,sizeof mix); mix[239]=1; swr_dsp_process(effect,mix,1,240);
    effect=swr_dsp_configure(effect,config);
    memset(mix,0,sizeof mix); swr_dsp_process(effect,mix,1,240);
    assert(fabsf(mix[47]-1)<1e-6f);
    swr_dsp_free(effect);

    /* Disabled effects still perform simultaneous channel remaps. */
    base(config,4,2); config[2]=0; config[0x26]=1; config[0x27]=0;
    effect=swr_dsp_configure(NULL,config); assert(effect);
    for(int i=0;i<240;i++){ mix[i]=.25f; mix[240+i]=.75f; }
    swr_dsp_process(effect,mix,2,240);
    for(int i=0;i<240;i++){ assert(mix[i]==.75f); assert(mix[240+i]==.25f); }
    config[0x26]=24; effect=swr_dsp_configure(effect,config);
    swr_dsp_process(effect,mix,2,240); assert(mix[0]==.75f);
    swr_dsp_free(effect);

    /* Both reverb engines accept every supported channel count and keep finite output. */
    unsigned channels[]={1,2,4,6};
    for(unsigned type=4;type<=5;type++) for(unsigned ch=0;ch<4;ch++){
        base(config,type,channels[ch]); p=config+0x20;
        if(type==4){
            put32(p+16,48<<14); put32(p+24,1<<14); put32(p+36,1<<14);
            put32(p+40,1<<14); put32(p+44,1<<14); put32(p+52,1<<14);
            put32(p+56,1<<14);
        } else {
            put32(p+20,48000); putf(p+28,5000); putf(p+32,1);
            putf(p+36,1); putf(p+52,50); putf(p+64,50);
        }
        effect=swr_dsp_configure(NULL,config); assert(effect);
        double energy=0;
        for(unsigned block=0;block<100;block++){
            memset(mix,0,sizeof mix); if(!block) mix[0]=1;
            swr_dsp_process(effect,mix,channels[ch],240);
            for(unsigned i=0;i<channels[ch]*240;i++){
                assert(isfinite(mix[i])); energy+=fabsf(mix[i]);
            }
        }
        assert(energy>0);
        /* Parameter changes and transitions between effect types release owned lines. */
        memset(p+16,0xff,0xa0-16);
        effect=swr_dsp_configure(effect,config); assert(effect);
        swr_dsp_process(effect,mix,channels[ch],240);
        for(unsigned i=0;i<channels[ch]*240;i++) assert(isfinite(mix[i]));
        config[0]=3; effect=swr_dsp_configure(effect,config); assert(effect);
        swr_dsp_process(effect,mix,channels[ch],240);
        put16(p+14,3); effect=swr_dsp_configure(effect,config); assert(!effect);
    }
    puts("Audio effect checks passed.");
    return 0;
}
