/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

// BeamNG macOS Vulkan compat layer (VK_LAYER_BEAMNG_macos_compat)
//
// Sits between Wine's winevulkan and MoltenVK (loaded by the Khronos loader bundled with Wine) and
// fills in the four device features BeamNG's Vulkan renderer requires but MoltenVK does not expose:
//
//   robustBufferAccess2   reported as supported, stripped before MoltenVK sees it
//   sparseBinding         sparse buffers are created as normal buffers, fully backed by real memory at
//   sparseResidencyBuffer creation; vkQueueBindSparse becomes an empty submit that still waits/signals
//                         the semaphores and fence, so everything is always "resident"
//   drawIndirectCount     vkCmdDraw(Indexed)IndirectCount -> vkCmdDraw(Indexed)Indirect(maxDrawCount)
//                         (BMCOMPAT_INDIRECT_COUNT=skip drops those draws instead, for comparison)
//
// Environment:
//   BMCOMPAT_LOG=1   log every interception to stderr
//
// Build: ./build.sh (x86_64, Wine runs under Rosetta)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#define LAYER_NAME "VK_LAYER_BEAMNG_macos_compat"
#define EXPORT __attribute__((visibility("default")))

static int g_log = 0;
static int g_skipIndirectCount = 0;
#define LOG(...) do { if (g_log) { fprintf(stderr, "[bmcompat] " __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define INFO(...) do { fprintf(stderr, "[bmcompat] " __VA_ARGS__); fputc('\n', stderr); } while (0)

// ---------------------------------------------------------------------------
// dispatch bookkeeping (dispatchable handles share the loader's dispatch table pointer as key)
// ---------------------------------------------------------------------------

typedef void *DispatchKey;
static inline DispatchKey key_of(const void *handle) { return *(DispatchKey *)handle; }

typedef struct InstanceData {
	DispatchKey key;
	VkInstance instance;
	PFN_vkGetInstanceProcAddr gipa;
	PFN_vkDestroyInstance DestroyInstance;
	PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures;
	PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties2 GetPhysicalDeviceQueueFamilyProperties2;
	PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
	PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;
	struct InstanceData *next;
} InstanceData;

typedef struct SparseBuffer {
	VkBuffer buffer;
	VkDeviceMemory memory;
	struct SparseBuffer *next;
} SparseBuffer;

typedef struct DeviceData {
	DispatchKey key;
	VkDevice device;
	VkPhysicalDevice physicalDevice;
	InstanceData *inst;
	PFN_vkGetDeviceProcAddr gdpa;
	PFN_vkDestroyDevice DestroyDevice;
	PFN_vkCreateBuffer CreateBuffer;
	PFN_vkDestroyBuffer DestroyBuffer;
	PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
	PFN_vkAllocateMemory AllocateMemory;
	PFN_vkFreeMemory FreeMemory;
	PFN_vkBindBufferMemory BindBufferMemory;
	PFN_vkQueueSubmit QueueSubmit;
	PFN_vkCmdDrawIndirect CmdDrawIndirect;
	PFN_vkCmdDrawIndexedIndirect CmdDrawIndexedIndirect;
	SparseBuffer *sparse;
	unsigned long indirectCountCalls, sparseBinds, sparseBuffers;
	struct DeviceData *next;
} DeviceData;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static InstanceData *g_instances = NULL;
static DeviceData *g_devices = NULL;

static InstanceData *get_instance(const void *handle) {
	DispatchKey k = key_of(handle);
	pthread_mutex_lock(&g_lock);
	InstanceData *d = g_instances;
	while (d && d->key != k) d = d->next;
	pthread_mutex_unlock(&g_lock);
	return d;
}

static DeviceData *get_device(const void *handle) {
	DispatchKey k = key_of(handle);
	pthread_mutex_lock(&g_lock);
	DeviceData *d = g_devices;
	while (d && d->key != k) d = d->next;
	pthread_mutex_unlock(&g_lock);
	return d;
}

static void init_env(void) {
	static int done = 0;
	if (done) return;
	done = 1;
	const char *l = getenv("BMCOMPAT_LOG");
	g_log = l && l[0] == '1';
	const char *ic = getenv("BMCOMPAT_INDIRECT_COUNT");
	g_skipIndirectCount = ic && strcmp(ic, "skip") == 0;
	INFO("loaded (indirect count mode: %s)", g_skipIndirectCount ? "skip" : "maxDrawCount");
}

// walk a pNext chain for a struct type
static void *find_in_chain(const void *pNext, VkStructureType type) {
	const VkBaseInStructure *s = (const VkBaseInStructure *)pNext;
	while (s) {
		if (s->sType == type) return (void *)s;
		s = s->pNext;
	}
	return NULL;
}

// ---------------------------------------------------------------------------
// instance level
// ---------------------------------------------------------------------------

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
	init_env();
	VkLayerInstanceCreateInfo *chain = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;
	while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
		chain = (VkLayerInstanceCreateInfo *)chain->pNext;
	if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

	PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
	PFN_vkCreateInstance createInstance = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
	VkResult r = createInstance(pCreateInfo, pAllocator, pInstance);
	if (r != VK_SUCCESS) return r;

	InstanceData *d = calloc(1, sizeof(*d));
	d->key = key_of(*pInstance);
	d->instance = *pInstance;
	d->gipa = gipa;
#define IGET(name) d->name = (PFN_vk##name)gipa(*pInstance, "vk" #name)
	IGET(DestroyInstance);
	IGET(GetPhysicalDeviceFeatures);
	IGET(GetPhysicalDeviceFeatures2);
	if (!d->GetPhysicalDeviceFeatures2) d->GetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)gipa(*pInstance, "vkGetPhysicalDeviceFeatures2KHR");
	IGET(GetPhysicalDeviceQueueFamilyProperties);
	IGET(GetPhysicalDeviceQueueFamilyProperties2);
	if (!d->GetPhysicalDeviceQueueFamilyProperties2) d->GetPhysicalDeviceQueueFamilyProperties2 = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)gipa(*pInstance, "vkGetPhysicalDeviceQueueFamilyProperties2KHR");
	IGET(GetPhysicalDeviceMemoryProperties);
	IGET(EnumerateDeviceExtensionProperties);
#undef IGET
	pthread_mutex_lock(&g_lock);
	d->next = g_instances;
	g_instances = d;
	pthread_mutex_unlock(&g_lock);
	LOG("instance created");
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL layer_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
	InstanceData *d = get_instance(instance);
	if (!d) return;
	d->DestroyInstance(instance, pAllocator);
	pthread_mutex_lock(&g_lock);
	for (InstanceData **p = &g_instances; *p; p = &(*p)->next)
		if (*p == d) { *p = d->next; break; }
	pthread_mutex_unlock(&g_lock);
	free(d);
}

static void patch_features(VkPhysicalDeviceFeatures *f) {
	f->sparseBinding = VK_TRUE;
	f->sparseResidencyBuffer = VK_TRUE;
}

static void patch_features_chain(void *pNext) {
	VkPhysicalDeviceVulkan12Features *v12 = find_in_chain(pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
	if (v12) v12->drawIndirectCount = VK_TRUE;
	VkPhysicalDeviceRobustness2FeaturesKHR *rb2 = find_in_chain(pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR);
	if (rb2) rb2->robustBufferAccess2 = VK_TRUE;
}

static VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFeatures(VkPhysicalDevice pd, VkPhysicalDeviceFeatures *pFeatures) {
	InstanceData *d = get_instance(pd);
	d->GetPhysicalDeviceFeatures(pd, pFeatures);
	patch_features(pFeatures);
}

static VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2 *pFeatures) {
	InstanceData *d = get_instance(pd);
	d->GetPhysicalDeviceFeatures2(pd, pFeatures);
	patch_features(&pFeatures->features);
	patch_features_chain(pFeatures->pNext);
}

// sparse binds are serviced on the graphics queue, so advertise the capability there
static VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pd, uint32_t *pCount, VkQueueFamilyProperties *pProps) {
	InstanceData *d = get_instance(pd);
	d->GetPhysicalDeviceQueueFamilyProperties(pd, pCount, pProps);
	if (pProps)
		for (uint32_t i = 0; i < *pCount; i++)
			if (pProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) pProps[i].queueFlags |= VK_QUEUE_SPARSE_BINDING_BIT;
}

static VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice pd, uint32_t *pCount, VkQueueFamilyProperties2 *pProps) {
	InstanceData *d = get_instance(pd);
	d->GetPhysicalDeviceQueueFamilyProperties2(pd, pCount, pProps);
	if (pProps)
		for (uint32_t i = 0; i < *pCount; i++)
			if (pProps[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) pProps[i].queueFamilyProperties.queueFlags |= VK_QUEUE_SPARSE_BINDING_BIT;
}

// ---------------------------------------------------------------------------
// device creation: strip the emulated features before MoltenVK validates them
// ---------------------------------------------------------------------------

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
	InstanceData *inst = get_instance(pd);
	VkLayerDeviceCreateInfo *chain = (VkLayerDeviceCreateInfo *)pCreateInfo->pNext;
	while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
		chain = (VkLayerDeviceCreateInfo *)chain->pNext;
	if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

	PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	PFN_vkGetDeviceProcAddr gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
	chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
	PFN_vkCreateDevice createDevice = (PFN_vkCreateDevice)gipa(inst->instance, "vkCreateDevice");

	// The create info is const, but the feature structs it points at are owned by the app and only read
	// by the driver during this call. Clear the emulated bits, call down, then restore them.
	VkPhysicalDeviceFeatures *f1 = (VkPhysicalDeviceFeatures *)pCreateInfo->pEnabledFeatures;
	VkPhysicalDeviceFeatures2 *f2 = find_in_chain(pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
	if (f2) f1 = &f2->features;
	VkPhysicalDeviceVulkan12Features *v12 = find_in_chain(pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
	VkPhysicalDeviceRobustness2FeaturesKHR *rb2 = find_in_chain(pCreateInfo->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR);

	VkBool32 sb = 0, srb = 0, dic = 0, rba2 = 0;
	if (f1) { sb = f1->sparseBinding; srb = f1->sparseResidencyBuffer; f1->sparseBinding = VK_FALSE; f1->sparseResidencyBuffer = VK_FALSE; }
	if (v12) { dic = v12->drawIndirectCount; v12->drawIndirectCount = VK_FALSE; }
	if (rb2) { rba2 = rb2->robustBufferAccess2; rb2->robustBufferAccess2 = VK_FALSE; }
	INFO("vkCreateDevice: emulating sparseBinding=%u sparseResidencyBuffer=%u drawIndirectCount=%u robustBufferAccess2=%u", sb, srb, dic, rba2);

	VkResult r = createDevice(pd, pCreateInfo, pAllocator, pDevice);

	if (f1) { f1->sparseBinding = sb; f1->sparseResidencyBuffer = srb; }
	if (v12) v12->drawIndirectCount = dic;
	if (rb2) rb2->robustBufferAccess2 = rba2;
	if (r != VK_SUCCESS) { INFO("vkCreateDevice failed below the layer: %d", r); return r; }

	DeviceData *d = calloc(1, sizeof(*d));
	d->key = key_of(*pDevice);
	d->device = *pDevice;
	d->physicalDevice = pd;
	d->inst = inst;
	d->gdpa = gdpa;
#define DGET(name) d->name = (PFN_vk##name)gdpa(*pDevice, "vk" #name)
	DGET(DestroyDevice);
	DGET(CreateBuffer);
	DGET(DestroyBuffer);
	DGET(GetBufferMemoryRequirements);
	DGET(AllocateMemory);
	DGET(FreeMemory);
	DGET(BindBufferMemory);
	DGET(QueueSubmit);
	DGET(CmdDrawIndirect);
	DGET(CmdDrawIndexedIndirect);
#undef DGET
	pthread_mutex_lock(&g_lock);
	d->next = g_devices;
	g_devices = d;
	pthread_mutex_unlock(&g_lock);
	INFO("device created");
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL layer_DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
	DeviceData *d = get_device(device);
	if (!d) return;
	INFO("device destroyed: %lu indirect-count draws, %lu sparse binds, %lu sparse buffers", d->indirectCountCalls, d->sparseBinds, d->sparseBuffers);
	for (SparseBuffer *s = d->sparse; s;) { SparseBuffer *n = s->next; d->FreeMemory(device, s->memory, NULL); free(s); s = n; }
	d->DestroyDevice(device, pAllocator);
	pthread_mutex_lock(&g_lock);
	for (DeviceData **p = &g_devices; *p; p = &(*p)->next)
		if (*p == d) { *p = d->next; break; }
	pthread_mutex_unlock(&g_lock);
	free(d);
}

// ---------------------------------------------------------------------------
// sparse buffers: normal buffers fully backed at creation
// ---------------------------------------------------------------------------

static uint32_t pick_memory_type(DeviceData *d, uint32_t typeBits) {
	VkPhysicalDeviceMemoryProperties mp;
	d->inst->GetPhysicalDeviceMemoryProperties(d->physicalDevice, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if (typeBits & (1u << i)) return i;
	return 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
	DeviceData *d = get_device(device);
	const VkBufferCreateFlags sparseFlags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT | VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
	if (!(pCreateInfo->flags & sparseFlags)) return d->CreateBuffer(device, pCreateInfo, pAllocator, pBuffer);

	VkBufferCreateInfo ci = *pCreateInfo;
	ci.flags &= ~sparseFlags;
	VkResult r = d->CreateBuffer(device, &ci, pAllocator, pBuffer);
	if (r != VK_SUCCESS) return r;

	VkMemoryRequirements req;
	d->GetBufferMemoryRequirements(device, *pBuffer, &req);
	VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, pick_memory_type(d, req.memoryTypeBits) };
	VkMemoryAllocateFlagsInfo flagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, NULL, 0, 0 };
	if (ci.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) { flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT; ai.pNext = &flagsInfo; }
	VkDeviceMemory mem;
	r = d->AllocateMemory(device, &ai, NULL, &mem);
	if (r != VK_SUCCESS) { INFO("sparse buffer backing allocation of %llu bytes failed: %d", (unsigned long long)req.size, r); d->DestroyBuffer(device, *pBuffer, pAllocator); return r; }
	d->BindBufferMemory(device, *pBuffer, mem, 0);

	SparseBuffer *s = calloc(1, sizeof(*s));
	s->buffer = *pBuffer;
	s->memory = mem;
	pthread_mutex_lock(&g_lock);
	s->next = d->sparse;
	d->sparse = s;
	d->sparseBuffers++;
	pthread_mutex_unlock(&g_lock);
	LOG("sparse buffer %p created fully resident (%llu bytes)", (void *)*pBuffer, (unsigned long long)req.size);
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL layer_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
	DeviceData *d = get_device(device);
	d->DestroyBuffer(device, buffer, pAllocator);
	if (buffer == VK_NULL_HANDLE) return;
	pthread_mutex_lock(&g_lock);
	for (SparseBuffer **p = &d->sparse; *p; p = &(*p)->next) {
		if ((*p)->buffer == buffer) {
			SparseBuffer *s = *p;
			*p = s->next;
			pthread_mutex_unlock(&g_lock);
			d->FreeMemory(device, s->memory, NULL);
			free(s);
			return;
		}
	}
	pthread_mutex_unlock(&g_lock);
}

// Nothing to bind (everything is already resident), but the wait/signal semantics must be kept.
static VKAPI_ATTR VkResult VKAPI_CALL layer_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo *pBindInfo, VkFence fence) {
	DeviceData *d = get_device(queue);
	d->sparseBinds += bindInfoCount;
	if (bindInfoCount == 0) return fence ? d->QueueSubmit(queue, 0, NULL, fence) : VK_SUCCESS;

	VkSubmitInfo *submits = calloc(bindInfoCount, sizeof(VkSubmitInfo));
	VkPipelineStageFlags *stages = NULL;
	uint32_t maxWaits = 0;
	for (uint32_t i = 0; i < bindInfoCount; i++) if (pBindInfo[i].waitSemaphoreCount > maxWaits) maxWaits = pBindInfo[i].waitSemaphoreCount;
	if (maxWaits) {
		stages = malloc(sizeof(VkPipelineStageFlags) * maxWaits);
		for (uint32_t i = 0; i < maxWaits; i++) stages[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
	for (uint32_t i = 0; i < bindInfoCount; i++) {
		const VkBindSparseInfo *b = &pBindInfo[i];
		submits[i].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submits[i].pNext = find_in_chain(b->pNext, VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO); // same struct is valid on VkSubmitInfo
		submits[i].waitSemaphoreCount = b->waitSemaphoreCount;
		submits[i].pWaitSemaphores = b->pWaitSemaphores;
		submits[i].pWaitDstStageMask = stages;
		submits[i].signalSemaphoreCount = b->signalSemaphoreCount;
		submits[i].pSignalSemaphores = b->pSignalSemaphores;
		LOG("vkQueueBindSparse -> empty submit (%u buffer binds ignored, %u image binds ignored)", b->bufferBindCount, b->imageBindCount);
	}
	VkResult r = d->QueueSubmit(queue, bindInfoCount, submits, fence);
	free(stages);
	free(submits);
	return r;
}

// ---------------------------------------------------------------------------
// draw indirect count
// ---------------------------------------------------------------------------

static VKAPI_ATTR void VKAPI_CALL layer_CmdDrawIndirectCount(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
	DeviceData *d = get_device(cb);
	d->indirectCountCalls++;
	if (g_skipIndirectCount || maxDrawCount == 0) return;
	d->CmdDrawIndirect(cb, buffer, offset, maxDrawCount, stride);
}

static VKAPI_ATTR void VKAPI_CALL layer_CmdDrawIndexedIndirectCount(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
	DeviceData *d = get_device(cb);
	d->indirectCountCalls++;
	if (g_skipIndirectCount || maxDrawCount == 0) return;
	d->CmdDrawIndexedIndirect(cb, buffer, offset, maxDrawCount, stride);
}

// ---------------------------------------------------------------------------
// proc address plumbing
// ---------------------------------------------------------------------------

static PFN_vkVoidFunction device_override(const char *name) {
#define OVR(fn, impl) if (!strcmp(name, fn)) return (PFN_vkVoidFunction)impl
	OVR("vkGetDeviceProcAddr", NULL); // handled by caller
	OVR("vkDestroyDevice", layer_DestroyDevice);
	OVR("vkCreateBuffer", layer_CreateBuffer);
	OVR("vkDestroyBuffer", layer_DestroyBuffer);
	OVR("vkQueueBindSparse", layer_QueueBindSparse);
	OVR("vkCmdDrawIndirectCount", layer_CmdDrawIndirectCount);
	OVR("vkCmdDrawIndirectCountKHR", layer_CmdDrawIndirectCount);
	OVR("vkCmdDrawIndirectCountAMD", layer_CmdDrawIndirectCount);
	OVR("vkCmdDrawIndexedIndirectCount", layer_CmdDrawIndexedIndirectCount);
	OVR("vkCmdDrawIndexedIndirectCountKHR", layer_CmdDrawIndexedIndirectCount);
	OVR("vkCmdDrawIndexedIndirectCountAMD", layer_CmdDrawIndexedIndirectCount);
#undef OVR
	return NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL bmcompat_GetDeviceProcAddr(VkDevice device, const char *pName) {
	if (!strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)bmcompat_GetDeviceProcAddr;
	PFN_vkVoidFunction f = device_override(pName);
	if (f) return f;
	DeviceData *d = get_device(device);
	return d ? d->gdpa(device, pName) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL bmcompat_GetInstanceProcAddr(VkInstance instance, const char *pName) {
#define OVR(fn, impl) if (!strcmp(pName, fn)) return (PFN_vkVoidFunction)impl
	OVR("vkGetInstanceProcAddr", bmcompat_GetInstanceProcAddr);
	OVR("vkGetDeviceProcAddr", bmcompat_GetDeviceProcAddr);
	OVR("vkCreateInstance", layer_CreateInstance);
	OVR("vkDestroyInstance", layer_DestroyInstance);
	OVR("vkCreateDevice", layer_CreateDevice);
	OVR("vkGetPhysicalDeviceFeatures", layer_GetPhysicalDeviceFeatures);
	OVR("vkGetPhysicalDeviceFeatures2", layer_GetPhysicalDeviceFeatures2);
	OVR("vkGetPhysicalDeviceFeatures2KHR", layer_GetPhysicalDeviceFeatures2);
	OVR("vkGetPhysicalDeviceQueueFamilyProperties", layer_GetPhysicalDeviceQueueFamilyProperties);
	OVR("vkGetPhysicalDeviceQueueFamilyProperties2", layer_GetPhysicalDeviceQueueFamilyProperties2);
	OVR("vkGetPhysicalDeviceQueueFamilyProperties2KHR", layer_GetPhysicalDeviceQueueFamilyProperties2);
#undef OVR
	PFN_vkVoidFunction f = device_override(pName);
	if (f) return f;
	if (instance == VK_NULL_HANDLE) return NULL;
	InstanceData *d = get_instance(instance);
	return d ? d->gipa(instance, pName) : NULL;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL bmcompat_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct) {
	init_env();
	if (pVersionStruct->loaderLayerInterfaceVersion > 2) pVersionStruct->loaderLayerInterfaceVersion = 2;
	pVersionStruct->pfnGetInstanceProcAddr = bmcompat_GetInstanceProcAddr;
	pVersionStruct->pfnGetDeviceProcAddr = bmcompat_GetDeviceProcAddr;
	pVersionStruct->pfnGetPhysicalDeviceProcAddr = NULL;
	return VK_SUCCESS;
}
