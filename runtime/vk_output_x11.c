/* Linux Vulkan-output prototype. The game remains rendered by OpenGL; this module
   reads the completed GL back buffer and uploads it to a Vulkan Xlib swapchain.
   This is a compatibility path; cross-API zero-copy sharing can replace the
   readback once the Linux driver combinations are validated. */
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <dlfcn.h>
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "rt.h"

typedef struct {
  VkInstance instance;
  VkSurfaceKHR surface;
  VkPhysicalDevice physical;
  VkDevice device;
  VkQueue queue;
  u32 queue_family;
  VkSwapchainKHR swapchain;
  VkFormat format;
  VkExtent2D extent;
  VkImage* images;
  VkImageLayout* layouts;
  u32 image_count;
  VkBuffer staging;
  VkDeviceMemory staging_memory;
  void* mapped;
  VkDeviceSize staging_size;
  int staging_coherent;
  VkCommandPool command_pool;
  VkCommandBuffer command;
  VkSemaphore acquired, complete;
  VkFence fence;
  u8* pixels;
  int enabled;
} SwrVkOutput;

static SwrVkOutput s_vk;

typedef struct {
  PFN_vkCreateInstance vkCreateInstance;
  PFN_vkDestroyInstance vkDestroyInstance;
  PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
  PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
  PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
  PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
  PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
  PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
  PFN_vkCreateDevice vkCreateDevice;
  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
  PFN_vkGetDeviceQueue vkGetDeviceQueue;
  PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
  PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
  PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
  PFN_vkCreateBuffer vkCreateBuffer;
  PFN_vkDestroyBuffer vkDestroyBuffer;
  PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
  PFN_vkAllocateMemory vkAllocateMemory;
  PFN_vkFreeMemory vkFreeMemory;
  PFN_vkBindBufferMemory vkBindBufferMemory;
  PFN_vkMapMemory vkMapMemory;
  PFN_vkUnmapMemory vkUnmapMemory;
  PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
  PFN_vkCreateCommandPool vkCreateCommandPool;
  PFN_vkDestroyCommandPool vkDestroyCommandPool;
  PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
  PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
  PFN_vkEndCommandBuffer vkEndCommandBuffer;
  PFN_vkResetCommandBuffer vkResetCommandBuffer;
  PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
  PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
  PFN_vkCreateSemaphore vkCreateSemaphore;
  PFN_vkDestroySemaphore vkDestroySemaphore;
  PFN_vkCreateFence vkCreateFence;
  PFN_vkDestroyFence vkDestroyFence;
  PFN_vkWaitForFences vkWaitForFences;
  PFN_vkResetFences vkResetFences;
  PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
  PFN_vkQueueSubmit vkQueueSubmit;
  PFN_vkQueuePresentKHR vkQueuePresentKHR;
  PFN_vkQueueWaitIdle vkQueueWaitIdle;
  PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
  PFN_vkDestroyDevice vkDestroyDevice;
} SwrVkApi;
static SwrVkApi s_api;
static void* s_vulkan_lib;
static PFN_vkGetInstanceProcAddr s_gipa;

#define vkCreateInstance s_api.vkCreateInstance
#define vkDestroyInstance s_api.vkDestroyInstance
#define vkEnumeratePhysicalDevices s_api.vkEnumeratePhysicalDevices
#define vkEnumerateDeviceExtensionProperties s_api.vkEnumerateDeviceExtensionProperties
#define vkGetPhysicalDeviceQueueFamilyProperties s_api.vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceMemoryProperties s_api.vkGetPhysicalDeviceMemoryProperties
#define vkGetPhysicalDeviceSurfaceSupportKHR s_api.vkGetPhysicalDeviceSurfaceSupportKHR
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR s_api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR s_api.vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkCreateXlibSurfaceKHR s_api.vkCreateXlibSurfaceKHR
#define vkDestroySurfaceKHR s_api.vkDestroySurfaceKHR
#define vkCreateDevice s_api.vkCreateDevice
#define vkGetDeviceProcAddr s_api.vkGetDeviceProcAddr
#define vkGetDeviceQueue s_api.vkGetDeviceQueue
#define vkCreateSwapchainKHR s_api.vkCreateSwapchainKHR
#define vkDestroySwapchainKHR s_api.vkDestroySwapchainKHR
#define vkGetSwapchainImagesKHR s_api.vkGetSwapchainImagesKHR
#define vkCreateBuffer s_api.vkCreateBuffer
#define vkDestroyBuffer s_api.vkDestroyBuffer
#define vkGetBufferMemoryRequirements s_api.vkGetBufferMemoryRequirements
#define vkAllocateMemory s_api.vkAllocateMemory
#define vkFreeMemory s_api.vkFreeMemory
#define vkBindBufferMemory s_api.vkBindBufferMemory
#define vkMapMemory s_api.vkMapMemory
#define vkUnmapMemory s_api.vkUnmapMemory
#define vkFlushMappedMemoryRanges s_api.vkFlushMappedMemoryRanges
#define vkCreateCommandPool s_api.vkCreateCommandPool
#define vkDestroyCommandPool s_api.vkDestroyCommandPool
#define vkAllocateCommandBuffers s_api.vkAllocateCommandBuffers
#define vkBeginCommandBuffer s_api.vkBeginCommandBuffer
#define vkEndCommandBuffer s_api.vkEndCommandBuffer
#define vkResetCommandBuffer s_api.vkResetCommandBuffer
#define vkCmdPipelineBarrier s_api.vkCmdPipelineBarrier
#define vkCmdCopyBufferToImage s_api.vkCmdCopyBufferToImage
#define vkCreateSemaphore s_api.vkCreateSemaphore
#define vkDestroySemaphore s_api.vkDestroySemaphore
#define vkCreateFence s_api.vkCreateFence
#define vkDestroyFence s_api.vkDestroyFence
#define vkWaitForFences s_api.vkWaitForFences
#define vkResetFences s_api.vkResetFences
#define vkAcquireNextImageKHR s_api.vkAcquireNextImageKHR
#define vkQueueSubmit s_api.vkQueueSubmit
#define vkQueuePresentKHR s_api.vkQueuePresentKHR
#define vkQueueWaitIdle s_api.vkQueueWaitIdle
#define vkDeviceWaitIdle s_api.vkDeviceWaitIdle
#define vkDestroyDevice s_api.vkDestroyDevice

void swr_vk_output_x11_shutdown(void);

#undef vkCreateInstance
#undef vkDestroyInstance
#undef vkEnumeratePhysicalDevices
#undef vkEnumerateDeviceExtensionProperties
#undef vkGetPhysicalDeviceQueueFamilyProperties
#undef vkGetPhysicalDeviceMemoryProperties
#undef vkGetPhysicalDeviceSurfaceSupportKHR
#undef vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#undef vkGetPhysicalDeviceSurfaceFormatsKHR
#undef vkCreateXlibSurfaceKHR
#undef vkDestroySurfaceKHR
#undef vkCreateDevice
#undef vkGetDeviceProcAddr
#undef vkGetDeviceQueue
#undef vkCreateSwapchainKHR
#undef vkDestroySwapchainKHR
#undef vkGetSwapchainImagesKHR
#undef vkCreateBuffer
#undef vkDestroyBuffer
#undef vkGetBufferMemoryRequirements
#undef vkAllocateMemory
#undef vkFreeMemory
#undef vkBindBufferMemory
#undef vkMapMemory
#undef vkUnmapMemory
#undef vkFlushMappedMemoryRanges
#undef vkCreateCommandPool
#undef vkDestroyCommandPool
#undef vkAllocateCommandBuffers
#undef vkBeginCommandBuffer
#undef vkEndCommandBuffer
#undef vkResetCommandBuffer
#undef vkCmdPipelineBarrier
#undef vkCmdCopyBufferToImage
#undef vkCreateSemaphore
#undef vkDestroySemaphore
#undef vkCreateFence
#undef vkDestroyFence
#undef vkWaitForFences
#undef vkResetFences
#undef vkAcquireNextImageKHR
#undef vkQueueSubmit
#undef vkQueuePresentKHR
#undef vkQueueWaitIdle
#undef vkDeviceWaitIdle
#undef vkDestroyDevice

static int load_vulkan(void){
  if(s_gipa) return 1;
  s_vulkan_lib=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);
  if(!s_vulkan_lib) s_vulkan_lib=dlopen("libvulkan.so",RTLD_NOW|RTLD_LOCAL);
  if(!s_vulkan_lib){ WARN("vulkan output: Vulkan loader not found; using OpenGL\n"); return 0; }
  s_gipa=(PFN_vkGetInstanceProcAddr)dlsym(s_vulkan_lib,"vkGetInstanceProcAddr");
  if(!s_gipa){ WARN("vulkan output: loader has no vkGetInstanceProcAddr; using OpenGL\n"); return 0; }
  s_api.vkCreateInstance=(PFN_vkCreateInstance)s_gipa(VK_NULL_HANDLE,"vkCreateInstance");
  if(!s_api.vkCreateInstance){ WARN("vulkan output: loader has no vkCreateInstance; using OpenGL\n"); return 0; }
  return 1;
}
static int load_instance_functions(void){
#define LOAD_I(fn) s_api.fn=(PFN_##fn)s_gipa(s_vk.instance,#fn); if(!s_api.fn){ WARN("vulkan output: loader is missing %s\n",#fn); return 0; }
  LOAD_I(vkDestroyInstance) LOAD_I(vkEnumeratePhysicalDevices) LOAD_I(vkEnumerateDeviceExtensionProperties)
  LOAD_I(vkGetPhysicalDeviceQueueFamilyProperties) LOAD_I(vkGetPhysicalDeviceMemoryProperties)
  LOAD_I(vkGetPhysicalDeviceSurfaceSupportKHR) LOAD_I(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
  LOAD_I(vkGetPhysicalDeviceSurfaceFormatsKHR) LOAD_I(vkCreateXlibSurfaceKHR) LOAD_I(vkDestroySurfaceKHR)
  LOAD_I(vkCreateDevice) LOAD_I(vkGetDeviceProcAddr)
#undef LOAD_I
  return 1;
}
static int load_device_functions(void){
#define LOAD_D(fn) s_api.fn=(PFN_##fn)s_api.vkGetDeviceProcAddr(s_vk.device,#fn); if(!s_api.fn){ WARN("vulkan output: device is missing %s\n",#fn); return 0; }
  LOAD_D(vkDestroyDevice) LOAD_D(vkDeviceWaitIdle)
  LOAD_D(vkGetDeviceQueue) LOAD_D(vkCreateSwapchainKHR) LOAD_D(vkDestroySwapchainKHR) LOAD_D(vkGetSwapchainImagesKHR)
  LOAD_D(vkCreateBuffer) LOAD_D(vkDestroyBuffer) LOAD_D(vkGetBufferMemoryRequirements) LOAD_D(vkAllocateMemory)
  LOAD_D(vkFreeMemory) LOAD_D(vkBindBufferMemory) LOAD_D(vkMapMemory) LOAD_D(vkUnmapMemory) LOAD_D(vkFlushMappedMemoryRanges)
  LOAD_D(vkCreateCommandPool) LOAD_D(vkDestroyCommandPool) LOAD_D(vkAllocateCommandBuffers) LOAD_D(vkBeginCommandBuffer)
  LOAD_D(vkEndCommandBuffer) LOAD_D(vkResetCommandBuffer) LOAD_D(vkCmdPipelineBarrier) LOAD_D(vkCmdCopyBufferToImage)
  LOAD_D(vkCreateSemaphore) LOAD_D(vkDestroySemaphore) LOAD_D(vkCreateFence) LOAD_D(vkDestroyFence) LOAD_D(vkWaitForFences)
  LOAD_D(vkResetFences) LOAD_D(vkAcquireNextImageKHR) LOAD_D(vkQueueSubmit) LOAD_D(vkQueuePresentKHR)
  LOAD_D(vkQueueWaitIdle)
#undef LOAD_D
  return 1;
}

#define vkCreateInstance s_api.vkCreateInstance
#define vkDestroyInstance s_api.vkDestroyInstance
#define vkEnumeratePhysicalDevices s_api.vkEnumeratePhysicalDevices
#define vkEnumerateDeviceExtensionProperties s_api.vkEnumerateDeviceExtensionProperties
#define vkGetPhysicalDeviceQueueFamilyProperties s_api.vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceMemoryProperties s_api.vkGetPhysicalDeviceMemoryProperties
#define vkGetPhysicalDeviceSurfaceSupportKHR s_api.vkGetPhysicalDeviceSurfaceSupportKHR
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR s_api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR s_api.vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkCreateXlibSurfaceKHR s_api.vkCreateXlibSurfaceKHR
#define vkDestroySurfaceKHR s_api.vkDestroySurfaceKHR
#define vkCreateDevice s_api.vkCreateDevice
#define vkGetDeviceProcAddr s_api.vkGetDeviceProcAddr
#define vkGetDeviceQueue s_api.vkGetDeviceQueue
#define vkCreateSwapchainKHR s_api.vkCreateSwapchainKHR
#define vkDestroySwapchainKHR s_api.vkDestroySwapchainKHR
#define vkGetSwapchainImagesKHR s_api.vkGetSwapchainImagesKHR
#define vkCreateBuffer s_api.vkCreateBuffer
#define vkDestroyBuffer s_api.vkDestroyBuffer
#define vkGetBufferMemoryRequirements s_api.vkGetBufferMemoryRequirements
#define vkAllocateMemory s_api.vkAllocateMemory
#define vkFreeMemory s_api.vkFreeMemory
#define vkBindBufferMemory s_api.vkBindBufferMemory
#define vkMapMemory s_api.vkMapMemory
#define vkUnmapMemory s_api.vkUnmapMemory
#define vkFlushMappedMemoryRanges s_api.vkFlushMappedMemoryRanges
#define vkCreateCommandPool s_api.vkCreateCommandPool
#define vkDestroyCommandPool s_api.vkDestroyCommandPool
#define vkAllocateCommandBuffers s_api.vkAllocateCommandBuffers
#define vkBeginCommandBuffer s_api.vkBeginCommandBuffer
#define vkEndCommandBuffer s_api.vkEndCommandBuffer
#define vkResetCommandBuffer s_api.vkResetCommandBuffer
#define vkCmdPipelineBarrier s_api.vkCmdPipelineBarrier
#define vkCmdCopyBufferToImage s_api.vkCmdCopyBufferToImage
#define vkCreateSemaphore s_api.vkCreateSemaphore
#define vkDestroySemaphore s_api.vkDestroySemaphore
#define vkCreateFence s_api.vkCreateFence
#define vkDestroyFence s_api.vkDestroyFence
#define vkWaitForFences s_api.vkWaitForFences
#define vkResetFences s_api.vkResetFences
#define vkAcquireNextImageKHR s_api.vkAcquireNextImageKHR
#define vkQueueSubmit s_api.vkQueueSubmit
#define vkQueuePresentKHR s_api.vkQueuePresentKHR
#define vkQueueWaitIdle s_api.vkQueueWaitIdle
#define vkDeviceWaitIdle s_api.vkDeviceWaitIdle
#define vkDestroyDevice s_api.vkDestroyDevice

static void vk_log_error(const char* what, VkResult r){ WARN("vulkan output: %s failed (%d)\n",what,(int)r); }
static int has_device_extension(VkPhysicalDevice d, const char* name){
  u32 n=0; if(vkEnumerateDeviceExtensionProperties(d,NULL,&n,NULL)!=VK_SUCCESS) return 0;
  VkExtensionProperties* p=calloc(n,sizeof *p); if(!p) return 0;
  int found=0; if(vkEnumerateDeviceExtensionProperties(d,NULL,&n,p)==VK_SUCCESS)
    for(u32 i=0;i<n;i++) if(!strcmp(p[i].extensionName,name)){ found=1; break; }
  free(p); return found;
}
static u32 memory_type(u32 bits, VkMemoryPropertyFlags flags, int* coherent){
  VkPhysicalDeviceMemoryProperties p; vkGetPhysicalDeviceMemoryProperties(s_vk.physical,&p);
  for(u32 i=0;i<p.memoryTypeCount;i++) if((bits&(1u<<i)) && (p.memoryTypes[i].propertyFlags&flags)==flags && (p.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { *coherent=1; return i; }
  for(u32 i=0;i<p.memoryTypeCount;i++) if((bits&(1u<<i)) && (p.memoryTypes[i].propertyFlags&flags)==flags) { *coherent=0; return i; }
  return UINT32_MAX;
}
static void destroy_swapchain(void){
  if(s_vk.device && vkDeviceWaitIdle) vkDeviceWaitIdle(s_vk.device);
  if(s_vk.swapchain) vkDestroySwapchainKHR(s_vk.device,s_vk.swapchain,NULL);
  s_vk.swapchain=VK_NULL_HANDLE; free(s_vk.images); free(s_vk.layouts); s_vk.images=NULL; s_vk.layouts=NULL; s_vk.image_count=0;
}
static int create_swapchain(void){
  VkSurfaceCapabilitiesKHR caps={0};
  VkResult r=vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_vk.physical,s_vk.surface,&caps);
  if(r!=VK_SUCCESS){ vk_log_error("surface capabilities",r); return 0; }
  if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT)){ WARN("vulkan output: swapchain images do not support transfer destination\n"); return 0; }
  u32 nf=0; r=vkGetPhysicalDeviceSurfaceFormatsKHR(s_vk.physical,s_vk.surface,&nf,NULL);
  if(r!=VK_SUCCESS || !nf){ vk_log_error("surface formats",r); return 0; }
  VkSurfaceFormatKHR* formats=calloc(nf,sizeof *formats); if(!formats) return 0;
  r=vkGetPhysicalDeviceSurfaceFormatsKHR(s_vk.physical,s_vk.surface,&nf,formats);
  if(r!=VK_SUCCESS){ free(formats); vk_log_error("surface formats",r); return 0; }
  VkSurfaceFormatKHR fmt=formats[0];
  if(fmt.format==VK_FORMAT_UNDEFINED) fmt.format=VK_FORMAT_R8G8B8A8_UNORM;
  for(u32 i=0;i<nf;i++) if(formats[i].format==VK_FORMAT_R8G8B8A8_UNORM){ fmt=formats[i]; break; }
  free(formats);
  s_vk.format=fmt.format;
  VkExtent2D e=caps.currentExtent;
  if(e.width==UINT32_MAX){ extern void backend_window_size(int*,int*); int w=1280,h=720; backend_window_size(&w,&h);
    e.width=(u32)w; e.height=(u32)h;
    if(e.width<caps.minImageExtent.width)e.width=caps.minImageExtent.width; if(e.width>caps.maxImageExtent.width)e.width=caps.maxImageExtent.width;
    if(e.height<caps.minImageExtent.height)e.height=caps.minImageExtent.height; if(e.height>caps.maxImageExtent.height)e.height=caps.maxImageExtent.height; }
  if(!e.width || !e.height) return 0;
  u32 count=caps.minImageCount+1; if(caps.maxImageCount && count>caps.maxImageCount) count=caps.maxImageCount;
  VkSwapchainCreateInfoKHR ci={0}; ci.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR; ci.surface=s_vk.surface;
  ci.minImageCount=count; ci.imageFormat=fmt.format; ci.imageColorSpace=fmt.colorSpace; ci.imageExtent=e;
  ci.imageArrayLayers=1; ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT; ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform=caps.currentTransform; ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if(!(caps.supportedCompositeAlpha&ci.compositeAlpha)){
    VkCompositeAlphaFlagBitsKHR opts[]={VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for(u32 i=0;i<3;i++) if(caps.supportedCompositeAlpha&opts[i]){ ci.compositeAlpha=opts[i]; break; }
  }
  ci.presentMode=VK_PRESENT_MODE_FIFO_KHR; ci.clipped=VK_TRUE;
  r=vkCreateSwapchainKHR(s_vk.device,&ci,NULL,&s_vk.swapchain); if(r!=VK_SUCCESS){ vk_log_error("swapchain creation",r); return 0; }
  s_vk.extent=e; r=vkGetSwapchainImagesKHR(s_vk.device,s_vk.swapchain,&s_vk.image_count,NULL);
  if(r!=VK_SUCCESS || !s_vk.image_count){ vk_log_error("swapchain images",r); return 0; }
  s_vk.images=calloc(s_vk.image_count,sizeof *s_vk.images); s_vk.layouts=calloc(s_vk.image_count,sizeof *s_vk.layouts);
  if(!s_vk.images || !s_vk.layouts) return 0;
  r=vkGetSwapchainImagesKHR(s_vk.device,s_vk.swapchain,&s_vk.image_count,s_vk.images);
  if(r!=VK_SUCCESS){ vk_log_error("swapchain image list",r); return 0; }
  LOG("vulkan output: Xlib swapchain ready (%ux%u, %u images, format %d)\n",e.width,e.height,s_vk.image_count,(int)s_vk.format);
  return 1;
}
static int create_staging(void){
  s_vk.staging_size=(VkDeviceSize)s_vk.extent.width*s_vk.extent.height*4;
  VkBufferCreateInfo bi={0}; bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bi.size=s_vk.staging_size; bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT; bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  VkResult r=vkCreateBuffer(s_vk.device,&bi,NULL,&s_vk.staging); if(r!=VK_SUCCESS){ vk_log_error("staging buffer",r); return 0; }
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(s_vk.device,s_vk.staging,&req);
  u32 mt=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&s_vk.staging_coherent);
  if(mt==UINT32_MAX){ WARN("vulkan output: no host-visible staging memory\n"); return 0; }
  VkMemoryAllocateInfo ai={0}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize=req.size; ai.memoryTypeIndex=mt;
  r=vkAllocateMemory(s_vk.device,&ai,NULL,&s_vk.staging_memory); if(r!=VK_SUCCESS){ vk_log_error("staging memory",r); return 0; }
  r=vkBindBufferMemory(s_vk.device,s_vk.staging,s_vk.staging_memory,0); if(r!=VK_SUCCESS){ vk_log_error("staging buffer bind",r); return 0; }
  r=vkMapMemory(s_vk.device,s_vk.staging_memory,0,VK_WHOLE_SIZE,0,&s_vk.mapped); if(r!=VK_SUCCESS){ vk_log_error("staging map",r); return 0; }
  s_vk.pixels=malloc((size_t)s_vk.staging_size); return s_vk.pixels!=NULL;
}
static int recreate_if_resized(void){
  VkSurfaceCapabilitiesKHR caps={0}; if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_vk.physical,s_vk.surface,&caps)!=VK_SUCCESS) return 0;
  int w=1280,h=720; extern void backend_window_size(int*,int*); backend_window_size(&w,&h);
  u32 sw=caps.currentExtent.width==UINT32_MAX?(u32)w:caps.currentExtent.width;
  u32 sh=caps.currentExtent.height==UINT32_MAX?(u32)h:caps.currentExtent.height;
  if(sw==s_vk.extent.width && sh==s_vk.extent.height) return 1;
  vkDeviceWaitIdle(s_vk.device);
  if(s_vk.mapped){ vkUnmapMemory(s_vk.device,s_vk.staging_memory); s_vk.mapped=NULL; }
  if(s_vk.staging) vkDestroyBuffer(s_vk.device,s_vk.staging,NULL); s_vk.staging=VK_NULL_HANDLE;
  if(s_vk.staging_memory) vkFreeMemory(s_vk.device,s_vk.staging_memory,NULL); s_vk.staging_memory=VK_NULL_HANDLE;
  free(s_vk.pixels); s_vk.pixels=NULL; destroy_swapchain();
  return create_swapchain() && create_staging();
}
int swr_vk_output_x11_init(void* display, u64 window){
  if(!display || !window) return 0;
  if(!load_vulkan()) return 0;
  VkApplicationInfo app={0}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.pApplicationName="SwitchRecomp"; app.applicationVersion=1; app.pEngineName="SwitchRecomp"; app.engineVersion=1; app.apiVersion=VK_API_VERSION_1_0;
  const char* ie[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
  VkInstanceCreateInfo ici={0}; ici.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ici.pApplicationInfo=&app; ici.enabledExtensionCount=2; ici.ppEnabledExtensionNames=ie;
  VkResult r=vkCreateInstance(&ici,NULL,&s_vk.instance); if(r!=VK_SUCCESS){ vk_log_error("instance creation",r); goto fail; }
  if(!load_instance_functions()) goto fail;
  VkXlibSurfaceCreateInfoKHR sci={0}; sci.sType=VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR; sci.dpy=(Display*)display; sci.window=(Window)window;
  r=vkCreateXlibSurfaceKHR(s_vk.instance,&sci,NULL,&s_vk.surface); if(r!=VK_SUCCESS){ vk_log_error("Xlib surface",r); goto fail; }
  u32 nd=0; r=vkEnumeratePhysicalDevices(s_vk.instance,&nd,NULL); if(r!=VK_SUCCESS || !nd){ vk_log_error("physical devices",r); goto fail; }
  VkPhysicalDevice* ds=calloc(nd,sizeof *ds); if(!ds) goto fail;
  r=vkEnumeratePhysicalDevices(s_vk.instance,&nd,ds); if(r!=VK_SUCCESS){ free(ds); vk_log_error("physical devices",r); goto fail; }
  for(u32 i=0;i<nd && !s_vk.physical;i++){
    if(!has_device_extension(ds[i],VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
    u32 nq=0; vkGetPhysicalDeviceQueueFamilyProperties(ds[i],&nq,NULL); VkQueueFamilyProperties* qp=calloc(nq,sizeof *qp); if(!qp) continue;
    vkGetPhysicalDeviceQueueFamilyProperties(ds[i],&nq,qp);
    for(u32 q=0;q<nq;q++){ VkBool32 support=VK_FALSE; if(qp[q].queueCount && (qp[q].queueFlags&VK_QUEUE_GRAPHICS_BIT) &&
      vkGetPhysicalDeviceSurfaceSupportKHR(ds[i],q,s_vk.surface,&support)==VK_SUCCESS && support){ s_vk.physical=ds[i]; s_vk.queue_family=q; break; } }
    free(qp);
  }
  free(ds); if(!s_vk.physical){ WARN("vulkan output: no X11 graphics/present queue found\n"); goto fail; }
  float priority=1.0f; VkDeviceQueueCreateInfo qi={0}; qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qi.queueFamilyIndex=s_vk.queue_family; qi.queueCount=1; qi.pQueuePriorities=&priority;
  const char* de[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME}; VkDeviceCreateInfo di={0}; di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi; di.enabledExtensionCount=1; di.ppEnabledExtensionNames=de;
  r=vkCreateDevice(s_vk.physical,&di,NULL,&s_vk.device); if(r!=VK_SUCCESS){ vk_log_error("device creation",r); goto fail; }
  if(!load_device_functions()) goto fail;
  vkGetDeviceQueue(s_vk.device,s_vk.queue_family,0,&s_vk.queue);
  VkCommandPoolCreateInfo pi={0}; pi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex=s_vk.queue_family;
  r=vkCreateCommandPool(s_vk.device,&pi,NULL,&s_vk.command_pool); if(r!=VK_SUCCESS){ vk_log_error("command pool",r); goto fail; }
  VkCommandBufferAllocateInfo ca={0}; ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; ca.commandPool=s_vk.command_pool; ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount=1;
  r=vkAllocateCommandBuffers(s_vk.device,&ca,&s_vk.command); if(r!=VK_SUCCESS){ vk_log_error("command buffer",r); goto fail; }
  VkSemaphoreCreateInfo sem={0}; sem.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  r=vkCreateSemaphore(s_vk.device,&sem,NULL,&s_vk.acquired); if(r!=VK_SUCCESS){ vk_log_error("acquire semaphore",r); goto fail; }
  r=vkCreateSemaphore(s_vk.device,&sem,NULL,&s_vk.complete); if(r!=VK_SUCCESS){ vk_log_error("present semaphore",r); goto fail; }
  VkFenceCreateInfo fi={0}; fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fi.flags=VK_FENCE_CREATE_SIGNALED_BIT;
  r=vkCreateFence(s_vk.device,&fi,NULL,&s_vk.fence); if(r!=VK_SUCCESS){ vk_log_error("frame fence",r); goto fail; }
  if(!create_swapchain() || !create_staging()) goto fail;
  s_vk.enabled=1; LOG("vulkan output: active (OpenGL render + Xlib Vulkan present; CPU frame upload)\n"); return 1;
fail:
  swr_vk_output_x11_shutdown(); return 0;
}
int swr_vk_output_x11_active(void){ return s_vk.enabled; }
int swr_vk_output_x11_present(void){
  if(!s_vk.enabled || !recreate_if_resized()) return 0;
  VkResult wr=vkWaitForFences(s_vk.device,1,&s_vk.fence,VK_TRUE,UINT64_MAX); if(wr!=VK_SUCCESS){ vk_log_error("frame fence wait",wr); return 0; }
  while(glGetError()!=GL_NO_ERROR){}
  GLint old_read=GL_BACK, old_align=4, old_row=0, old_skip_px=0, old_skip_rows=0;
  glGetIntegerv(GL_READ_BUFFER,&old_read); glGetIntegerv(GL_PACK_ALIGNMENT,&old_align);
  glGetIntegerv(GL_PACK_ROW_LENGTH,&old_row); glGetIntegerv(GL_PACK_SKIP_PIXELS,&old_skip_px); glGetIntegerv(GL_PACK_SKIP_ROWS,&old_skip_rows);
  glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT,1);
  glPixelStorei(GL_PACK_ROW_LENGTH,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0); glPixelStorei(GL_PACK_SKIP_ROWS,0);
  glReadPixels(0,0,(GLsizei)s_vk.extent.width,(GLsizei)s_vk.extent.height,GL_RGBA,GL_UNSIGNED_BYTE,s_vk.pixels);
  GLenum ge=glGetError(); glReadBuffer((GLenum)old_read); glPixelStorei(GL_PACK_ALIGNMENT,old_align);
  glPixelStorei(GL_PACK_ROW_LENGTH,old_row); glPixelStorei(GL_PACK_SKIP_PIXELS,old_skip_px); glPixelStorei(GL_PACK_SKIP_ROWS,old_skip_rows);
  if(ge!=GL_NO_ERROR){ WARN("vulkan output: OpenGL back-buffer read failed (%x)\n",ge); return 0; }
  u32* src=(u32*)s_vk.pixels; u32* dst=(u32*)s_vk.mapped; size_t stride=s_vk.extent.width;
  for(u32 y=0;y<s_vk.extent.height;y++) for(u32 x=0;x<s_vk.extent.width;x++){
    u32 p=src[(size_t)(s_vk.extent.height-1-y)*stride+x];
    if(s_vk.format==VK_FORMAT_B8G8R8A8_UNORM || s_vk.format==VK_FORMAT_B8G8R8A8_SRGB) p=(p&0xFF00FF00u)|((p&0xFFu)<<16)|((p>>16)&0xFFu);
    dst[(size_t)y*stride+x]=p;
  }
  if(!s_vk.staging_coherent){ VkMappedMemoryRange range={0}; range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; range.memory=s_vk.staging_memory; range.offset=0; range.size=VK_WHOLE_SIZE;
    VkResult fr=vkFlushMappedMemoryRanges(s_vk.device,1,&range); if(fr!=VK_SUCCESS){ vk_log_error("staging flush",fr); return 0; } }
  u32 image=0; VkResult r=vkAcquireNextImageKHR(s_vk.device,s_vk.swapchain,UINT64_MAX,s_vk.acquired,VK_NULL_HANDLE,&image);
  if(r==VK_ERROR_OUT_OF_DATE_KHR){ s_vk.extent.width=0; return recreate_if_resized(); }
  if(r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR){ vk_log_error("acquire image",r); return 0; }
  vkResetFences(s_vk.device,1,&s_vk.fence); vkResetCommandBuffer(s_vk.command,0);
  VkCommandBufferBeginInfo bi={0}; bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  r=vkBeginCommandBuffer(s_vk.command,&bi); if(r!=VK_SUCCESS){ vk_log_error("begin command buffer",r); return 0; }
  VkImageMemoryBarrier before={0}; before.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; before.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  before.oldLayout=s_vk.layouts[image]; before.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; before.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; before.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  before.image=s_vk.images[image]; before.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  vkCmdPipelineBarrier(s_vk.command,s_vk.layouts[image]==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&before);
  VkBufferImageCopy copy={0}; copy.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent=(VkExtent3D){s_vk.extent.width,s_vk.extent.height,1};
  vkCmdCopyBufferToImage(s_vk.command,s_vk.staging,s_vk.images[image],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  VkImageMemoryBarrier after=before; after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; after.dstAccessMask=0; after.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; after.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  vkCmdPipelineBarrier(s_vk.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&after);
  r=vkEndCommandBuffer(s_vk.command); if(r!=VK_SUCCESS){ vk_log_error("end command buffer",r); return 0; }
  VkPipelineStageFlags wait=VK_PIPELINE_STAGE_TRANSFER_BIT; VkSubmitInfo si={0}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount=1; si.pWaitSemaphores=&s_vk.acquired; si.pWaitDstStageMask=&wait; si.commandBufferCount=1; si.pCommandBuffers=&s_vk.command; si.signalSemaphoreCount=1; si.pSignalSemaphores=&s_vk.complete;
  r=vkQueueSubmit(s_vk.queue,1,&si,s_vk.fence); if(r!=VK_SUCCESS){ vk_log_error("queue submit",r); return 0; }
  VkPresentInfoKHR present={0}; present.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR; present.waitSemaphoreCount=1; present.pWaitSemaphores=&s_vk.complete; present.swapchainCount=1; present.pSwapchains=&s_vk.swapchain; present.pImageIndices=&image;
  r=vkQueuePresentKHR(s_vk.queue,&present); s_vk.layouts[image]=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkResult idle=vkQueueWaitIdle(s_vk.queue); if(idle!=VK_SUCCESS){ vk_log_error("present queue wait",idle); return 0; }
  if(r==VK_ERROR_OUT_OF_DATE_KHR || r==VK_SUBOPTIMAL_KHR){ s_vk.extent.width=0; return recreate_if_resized(); }
  if(r!=VK_SUCCESS){ vk_log_error("present",r); return 0; }
  return 1;
}
void swr_vk_output_x11_shutdown(void){
  SwrVkOutput* v=&s_vk;
  if(v->device && vkDeviceWaitIdle) vkDeviceWaitIdle(v->device);
  destroy_swapchain();
  if(v->mapped && v->device && vkUnmapMemory) vkUnmapMemory(v->device,v->staging_memory);
  if(v->staging && v->device && vkDestroyBuffer) vkDestroyBuffer(v->device,v->staging,NULL);
  if(v->staging_memory && v->device && vkFreeMemory) vkFreeMemory(v->device,v->staging_memory,NULL);
  if(v->command_pool && v->device && vkDestroyCommandPool) vkDestroyCommandPool(v->device,v->command_pool,NULL);
  if(v->fence && v->device && vkDestroyFence) vkDestroyFence(v->device,v->fence,NULL);
  if(v->acquired && v->device && vkDestroySemaphore) vkDestroySemaphore(v->device,v->acquired,NULL);
  if(v->complete && v->device && vkDestroySemaphore) vkDestroySemaphore(v->device,v->complete,NULL);
  if(v->device && vkDestroyDevice) vkDestroyDevice(v->device,NULL);
  if(v->surface && v->instance) vkDestroySurfaceKHR(v->instance,v->surface,NULL);
  if(v->instance) vkDestroyInstance(v->instance,NULL);
  free(v->pixels); memset(v,0,sizeof *v);
}

#undef vkCreateInstance
#undef vkDestroyInstance
#undef vkEnumeratePhysicalDevices
#undef vkEnumerateDeviceExtensionProperties
#undef vkGetPhysicalDeviceQueueFamilyProperties
#undef vkGetPhysicalDeviceMemoryProperties
#undef vkGetPhysicalDeviceSurfaceSupportKHR
#undef vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#undef vkGetPhysicalDeviceSurfaceFormatsKHR
#undef vkCreateXlibSurfaceKHR
#undef vkDestroySurfaceKHR
#undef vkCreateDevice
#undef vkGetDeviceProcAddr
#undef vkGetDeviceQueue
#undef vkCreateSwapchainKHR
#undef vkDestroySwapchainKHR
#undef vkGetSwapchainImagesKHR
#undef vkCreateBuffer
#undef vkDestroyBuffer
#undef vkGetBufferMemoryRequirements
#undef vkAllocateMemory
#undef vkFreeMemory
#undef vkBindBufferMemory
#undef vkMapMemory
#undef vkUnmapMemory
#undef vkFlushMappedMemoryRanges
#undef vkCreateCommandPool
#undef vkDestroyCommandPool
#undef vkAllocateCommandBuffers
#undef vkBeginCommandBuffer
#undef vkEndCommandBuffer
#undef vkResetCommandBuffer
#undef vkCmdPipelineBarrier
#undef vkCmdCopyBufferToImage
#undef vkCreateSemaphore
#undef vkDestroySemaphore
#undef vkCreateFence
#undef vkDestroyFence
#undef vkWaitForFences
#undef vkResetFences
#undef vkAcquireNextImageKHR
#undef vkQueueSubmit
#undef vkQueuePresentKHR
#undef vkQueueWaitIdle
#undef vkDeviceWaitIdle
#undef vkDestroyDevice
