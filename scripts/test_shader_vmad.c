/* Execute generated integer VMAD expressions on the host OpenGL driver. */
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include "../runtime/shader.c"
#include <assert.h>
int g_texoff_emul;
static u32 extract(u32 x,u32 width,u32 sel,int sign){
 unsigned bits=width==3?32:width==2?16:8;
 if(bits==32) return x;
 x>>=sel*bits;u32 mask=(1u<<bits)-1;x&=mask;
 return sign && (x&(1u<<(bits-1)))?x|~mask:x;
}
int main(void){
 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
 assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
 EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
 EGLConfig config;EGLint count;assert(eglChooseConfig(display,config_attrs,&config,1,&count)&&count);
 EGLint surface_attrs[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attrs);
 EGLint context_attrs[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
 EGLContext gl=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);assert(gl!=EGL_NO_CONTEXT);
 assert(eglMakeCurrent(display,surface,surface,gl));
 ops_init(); Sb source={0}; sb_f(&source,"#version 430\nlayout(local_size_x=1) in; layout(std430,binding=0) buffer Output { uint values[]; }; void main(){uint i=gl_GlobalInvocationID.x;\n");
 u32 expected[320]; unsigned n=0;
 for(unsigned immediate=0;immediate<2;immediate++) for(unsigned af=0;af<8;af++) for(unsigned bf=0;bf<(immediate?1:8);bf++) for(unsigned sign=0;sign<4;sign++){
  u64 w=0x5f00000000070000ull|((u64)1<<8)|((u64)3<<39)|((u64)af<<36)|((u64)sign<<48);
  w|=immediate?((u64)0x81ef<<20):((u64)1<<50)|((u64)bf<<28)|((u64)2<<20);
  ShaderInfo info={0};Ctx ctx={0};ctx.si=&info;
  const Op* op=decode(w);assert(op && op->id==O_VMAD);inst(&ctx,0,w,op);assert(!info.unsupported);
  sb_f(&source,"if(i==%uu){uint r0=0u,r1=4275878552u,r2=2166572527u,r3=4294967294u;\n%s values[i]=r0;}\n",n,ctx.out.p);
  u32 a=extract(0xfedcba98,af>>1,af&3,sign&1);
  u32 b=extract(immediate?0x81ef:0x812345ef,immediate?2:bf>>1,immediate?0:bf&3,sign&2);
  expected[n++]=a*b+0xfffffffeu;free(ctx.out.p);
 }
 for(unsigned bit=47;bit<=55;bit++) if(bit==47 || bit>=51){
  ShaderInfo info={0};Ctx ctx={0};ctx.si=&info;u64 w=0x5f00000000070000ull|(1ull<<bit);inst(&ctx,0,w,decode(w));assert(info.unsupported==1);free(ctx.out.p);
 }
 sb_f(&source,"}\n");GLuint shader=glCreateShader(GL_COMPUTE_SHADER);const char* text=source.p;glShaderSource(shader,1,&text,NULL);glCompileShader(shader);
 GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char error[4096];glGetShaderInfoLog(shader,sizeof error,NULL,error);fprintf(stderr,"%s\n",error);}assert(ok);
 GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);
 GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);glBufferData(GL_SHADER_STORAGE_BUFFER,n*4,NULL,GL_DYNAMIC_COPY);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);
 glUseProgram(program);glDispatchCompute(n,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);u32 results[320];glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,n*4,results);
 for(unsigned i=0;i<n;i++) if(results[i]!=expected[i]){fprintf(stderr,"VMAD %u got %08x expected %08x\n",i,results[i],expected[i]);abort();}
 assert(glGetError()==GL_NO_ERROR);free(source.p);glDeleteBuffers(1,&buffer);glDeleteProgram(program);glDeleteShader(shader);
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,gl);eglDestroySurface(display,surface);eglTerminate(display);
 printf("VMAD: %u GPU arithmetic cases passed; unverified modifiers rejected.\n",n);
}
