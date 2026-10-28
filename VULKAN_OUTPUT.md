# Vulkan output on Linux

Set `renderer=vulkan` in a game's `.ini` file to try Vulkan presentation on the X11 backend. `renderer=opengl` remains the default. If Vulkan initialization or presentation fails, the runtime logs the reason and falls back to OpenGL.

The current implementation keeps the existing OpenGL game renderer. At each completed frame it reads pixels from the GL back buffer, flips the rows, uploads them through a Vulkan staging buffer, and presents an Xlib swapchain image. This is an initial compatibility implementation; the CPU readback and upload can reduce frame rate substantially. It does not add `vulkan_native` rendering or fix issues inside the OpenGL renderer.

Successful startup is reported as `vulkan output: active (OpenGL render + Xlib Vulkan present; CPU frame upload)`. A line showing only `OpenGL: ...` does not by itself confirm Vulkan output.

Building the Linux X11 runtime now requires Vulkan development headers in addition to X11 and OpenGL development files. The loader is loaded only when Vulkan output is selected, so OpenGL remains usable on systems without Vulkan. Selecting Vulkan requires a Vulkan-capable driver and loader; the renderer falls back to OpenGL if initialization fails.
