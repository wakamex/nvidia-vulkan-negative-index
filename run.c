// Runs a one-thread compute shader (push constant: a 64-bit buffer address) on a
// 256-word buffer and prints word 191. The probes store 22 at word 205 and read it
// back at 210 - 5, so a correct driver prints 22.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define LIB() LoadLibraryA("vulkan-1.dll")
#define SYM(l, n) (void*)GetProcAddress((HMODULE)(l), n)
#else
#include <dlfcn.h>
#define LIB() dlopen("libvulkan.so.1", RTLD_NOW)
#define SYM(l, n) dlsym(l, n)
#endif

#define CK(x) do { VkResult r_ = (x); if (r_) { printf("%s: %d\n", #x, r_); exit(1); } } while (0)
#define F(n) PFN_##n n
F(vkGetInstanceProcAddr); F(vkCreateInstance); F(vkEnumeratePhysicalDevices); F(vkGetPhysicalDeviceProperties);
F(vkGetPhysicalDeviceMemoryProperties); F(vkCreateDevice); F(vkGetDeviceQueue); F(vkCreateBuffer);
F(vkGetBufferMemoryRequirements); F(vkAllocateMemory); F(vkBindBufferMemory); F(vkMapMemory);
F(vkGetBufferDeviceAddress); F(vkCreateShaderModule); F(vkCreatePipelineLayout); F(vkCreateComputePipelines);
F(vkCreateCommandPool); F(vkAllocateCommandBuffers); F(vkBeginCommandBuffer); F(vkCmdBindPipeline);
F(vkCmdPushConstants); F(vkCmdDispatch); F(vkCmdPipelineBarrier); F(vkEndCommandBuffer); F(vkQueueSubmit); F(vkQueueWaitIdle);

int main(int argc, char** argv) {
  void* lib = LIB();
  if (!lib) { printf("no Vulkan loader\n"); return 1; }
  vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SYM(lib, "vkGetInstanceProcAddr");
#define GI(n) n = (PFN_##n)vkGetInstanceProcAddr(inst, #n)
  VkInstance inst = NULL;
  GI(vkCreateInstance);
  VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
  CK(vkCreateInstance(&(VkInstanceCreateInfo){ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app }, NULL, &inst));
  GI(vkEnumeratePhysicalDevices); GI(vkGetPhysicalDeviceProperties); GI(vkGetPhysicalDeviceMemoryProperties);
  GI(vkCreateDevice); GI(vkGetDeviceQueue); GI(vkCreateBuffer); GI(vkGetBufferMemoryRequirements);
  GI(vkAllocateMemory); GI(vkBindBufferMemory); GI(vkMapMemory); GI(vkGetBufferDeviceAddress);
  GI(vkCreateShaderModule); GI(vkCreatePipelineLayout); GI(vkCreateComputePipelines); GI(vkCreateCommandPool);
  GI(vkAllocateCommandBuffers); GI(vkBeginCommandBuffer); GI(vkCmdBindPipeline); GI(vkCmdPushConstants);
  GI(vkCmdDispatch); GI(vkCmdPipelineBarrier); GI(vkEndCommandBuffer); GI(vkQueueSubmit); GI(vkQueueWaitIdle);

  uint32_t n = 8;
  VkPhysicalDevice pds[8];
  CK(vkEnumeratePhysicalDevices(inst, &n, pds));
  VkPhysicalDevice pd = pds[0];
  VkPhysicalDeviceProperties pp;
  vkGetPhysicalDeviceProperties(pd, &pp);
  printf("%s, driver %u.%u\n", pp.deviceName, pp.driverVersion >> 22, (pp.driverVersion >> 14) & 0xff);

  VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .bufferDeviceAddress = 1 };
  VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12, .features.shaderInt64 = 1 };
  float prio = 1;
  VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
  VkDevice dev;
  CK(vkCreateDevice(pd, &(VkDeviceCreateInfo){ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi }, NULL, &dev));
  VkQueue q;
  vkGetDeviceQueue(dev, 0, 0, &q);

  VkBuffer buf;
  CK(vkCreateBuffer(dev, &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 2048,
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT }, NULL, &buf));
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(dev, buf, &mr);
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  uint32_t want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mt = 0;
  while (!((mr.memoryTypeBits >> mt) & 1) || (mp.memoryTypes[mt].propertyFlags & want) != want) mt++;
  VkMemoryAllocateFlagsInfo fl = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT };
  VkDeviceMemory mem;
  CK(vkAllocateMemory(dev, &(VkMemoryAllocateInfo){ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &fl, mr.size, mt }, NULL, &mem));
  CK(vkBindBufferMemory(dev, buf, mem, 0));
  uint64_t* map;
  CK(vkMapMemory(dev, mem, 0, 2048, 0, (void**)&map));
  VkDeviceAddress addr = vkGetBufferDeviceAddress(dev, &(VkBufferDeviceAddressInfo){ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buf });

  VkPipelineLayout pl;
  VkPushConstantRange pc = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 8 };
  CK(vkCreatePipelineLayout(dev, &(VkPipelineLayoutCreateInfo){ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc }, NULL, &pl));
  VkCommandPool pool;
  CK(vkCreateCommandPool(dev, &(VkCommandPoolCreateInfo){ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 }, NULL, &pool));

  for (int a = 1; a < argc; a++) {
    FILE* in = fopen(argv[a], "rb");
    if (!in) { printf("%s: missing\n", argv[a]); continue; }
    static uint32_t code[1 << 16];
    size_t bytes = fread(code, 1, sizeof code, in);
    fclose(in);
    VkShaderModule sm;
    CK(vkCreateShaderModule(dev, &(VkShaderModuleCreateInfo){ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = code }, NULL, &sm));
    VkPipeline pipe;
    CK(vkCreateComputePipelines(dev, NULL, 1, &(VkComputePipelineCreateInfo){ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main" },
      .layout = pl }, NULL, &pipe));
    VkCommandBuffer cb;
    CK(vkAllocateCommandBuffers(dev, &(VkCommandBufferAllocateInfo){ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .commandBufferCount = 1 }, &cb));
    memset(map, 0, 2048);
    CK(vkBeginCommandBuffer(cb, &(VkCommandBufferBeginInfo){ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }));
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, &addr);
    vkCmdDispatch(cb, 1, 1, 1);
    // Make the shader's writes visible to the host before it reads word 191.
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
      &(VkMemoryBarrier){ VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT },
      0, NULL, 0, NULL);
    CK(vkEndCommandBuffer(cb));
    CK(vkQueueSubmit(q, 1, &(VkSubmitInfo){ VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb }, NULL));
    CK(vkQueueWaitIdle(q));
    printf("%s: word 191 = %llu (22 is right)\n", argv[a], (unsigned long long)map[191]);
  }
  return 0;
}
