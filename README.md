# Vulkan: OpPtrAccessChain with a negative element on a PhysicalStorageBuffer pointer reads the wrong address

A compute shader that steps a buffer-device-address pointer forward and then indexes it with a negative element doesn't read the word it should. In a large buffer it silently reads the wrong data (0 in my case). In a small one the read faults and the device is lost (`VK_ERROR_DEVICE_LOST` from `vkQueueWaitIdle`). The same address computed with integer arithmetic reads correctly, and so do positive elements.

I hit this porting the GPU runtime of [Bend](https://github.com/bendlang/bend), a parallel programming language, to Vulkan. Its call stack reads below its stack pointer, so every function return jumped to the wrong place and the kernel hung until the device reset.

## Results

| Shader (store 22 at word 205, then read it back through `(base + 210)[-5]`) | Linux, RTX 3090, 610.57.04 | Windows 11, RTX 3080, 591.86 | Windows 11, RTX 3080, 617.14 |
|---|---|---|---|
| Positive element: `(base + 200)[5]` | 22, correct | 22, correct | 22, correct |
| Integer address: `*(u64*)((u64)p - 40)` | 22, correct | 22, correct | 22, correct |
| Negative 64-bit element: `p[(int64_t)-5]` | device lost | device lost | device lost |
| Negative 32-bit element: `p[(int)-5]` | device lost | device lost | device lost |

Linux is Fedora 44 (kernel 7.1.9) and Windows is 11 Pro build 26200. In an earlier run on Linux with a 3 GB buffer, the negative read returned 0 instead of losing the device, and in a larger shader it produced out-of-range addresses such as 0x86a08000 plus a small offset.

The SPIR-V passes `spirv-val --target-env vulkan1.3`. The pointer type is decorated `ArrayStride 8` and the element is a signed 64-bit constant -5, so as far as I can tell it's valid and the result pointer should be 40 bytes below `%sp_0`.

## Shader

[Slang](https://shader-slang.org) 2026.18.3, compiled with `slangc shaders/negative64.slang -target spirv -entry main -stage compute -O2`:

```slang
[[vk::push_constant]] uint64_t* out;
[numthreads(1,1,1)]
void main() {
  uint64_t* sp = out + 200;
  sp[(int64_t)5] = 22;
  sp = sp + (int64_t)10;
  out[191] = sp[(int64_t)(-5)];   // should read the 22 stored at word 205
}
```

That's `shaders/negative64.slang`; the other three shaders in `shaders/` differ only in the last line. The SPIR-V it produces (`shaders/negative64.spvasm`):

```
OpCapability PhysicalStorageBufferAddresses
OpCapability Int64
OpCapability Shader
OpExtension "SPV_KHR_physical_storage_buffer"
OpMemoryModel PhysicalStorageBuffer64 GLSL450
OpEntryPoint GLCompute %main "main" %out
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %_ptr_PhysicalStorageBuffer_ulong ArrayStride 8
OpDecorate %cbuffer__t Block
OpMemberDecorate %cbuffer__t 0 Offset 0
%void = OpTypeVoid
%8 = OpTypeFunction %void
%ulong = OpTypeInt 64 0
%_ptr_PhysicalStorageBuffer_ulong = OpTypePointer PhysicalStorageBuffer %ulong
%cbuffer__t = OpTypeStruct %_ptr_PhysicalStorageBuffer_ulong
%_ptr_PushConstant_cbuffer__t = OpTypePointer PushConstant %cbuffer__t
%int = OpTypeInt 32 1
%int_0 = OpConstant %int 0
%_ptr_PushConstant__ptr_PhysicalStorageBuffer_ulong = OpTypePointer PushConstant %_ptr_PhysicalStorageBuffer_ulong
%int_200 = OpConstant %int 200
%long = OpTypeInt 64 1
%long_5 = OpConstant %long 5
%ulong_22 = OpConstant %ulong 22
%long_10 = OpConstant %long 10
%int_191 = OpConstant %int 191
%long_n5 = OpConstant %long -5
%out = OpVariable %_ptr_PushConstant_cbuffer__t PushConstant
%main = OpFunction %void None %8
%21 = OpLabel
%22 = OpAccessChain %_ptr_PushConstant__ptr_PhysicalStorageBuffer_ulong %out %int_0
%23 = OpLoad %_ptr_PhysicalStorageBuffer_ulong %22
%sp = OpPtrAccessChain %_ptr_PhysicalStorageBuffer_ulong %23 %int_200
%24 = OpPtrAccessChain %_ptr_PhysicalStorageBuffer_ulong %sp %long_5
OpStore %24 %ulong_22 Aligned 8
%sp_0 = OpPtrAccessChain %_ptr_PhysicalStorageBuffer_ulong %sp %long_10
%25 = OpPtrAccessChain %_ptr_PhysicalStorageBuffer_ulong %23 %int_191
%26 = OpPtrAccessChain %_ptr_PhysicalStorageBuffer_ulong %sp_0 %long_n5
%27 = OpLoad %ulong %26 Aligned 8
OpStore %25 %27 Aligned 8
OpReturn
OpFunctionEnd
```

## Host side

A 2 KB host-visible, host-coherent buffer with `STORAGE_BUFFER | SHADER_DEVICE_ADDRESS` usage, allocated with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`, zeroed. Its device address goes in an 8-byte push constant, then one `vkCmdDispatch(1, 1, 1)`, submit, `vkQueueWaitIdle`, and word 191 is read back. Vulkan 1.3 device with `bufferDeviceAddress` and `shaderInt64` enabled, no descriptor sets. The runner is `run.c`, about 130 lines of C for Linux and Windows.

## Workaround

Build the address as an integer and convert it back to a pointer (`OpConvertPtrToU`, `OpIAdd`, `OpConvertUToPtr`). That reads correctly on all three drivers.

## Reproduce

The `.spv` files are committed, so only a C compiler, the Vulkan loader and the Vulkan headers are needed. A device-lost run leaves the device unusable, so run each shader in its own process:

```sh
cc -O2 run.c -ldl -o run
for f in shaders/*.spv; do ./run $f; done
```

A correct driver prints `word 191 = 22` for all four. For Windows, `zig cc -O2 -target x86_64-windows-gnu run.c -o run.exe` builds the same runner. To rebuild a shader: `slangc shaders/negative64.slang -target spirv -entry main -stage compute -O2 -o shaders/negative64.spv`.
