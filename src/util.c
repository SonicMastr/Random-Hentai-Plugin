#include "util.h"

__attribute__ ((__format__ (__printf__, 1, 2)))
void LOG(const char *fmt, ...) {
	(void)fmt;

	#ifdef LOG_PRINTF
	sceClibPrintf("\033[0;36m[GXMTest]\033[0m ");
	va_list args;
	va_start(args, fmt);
	sceClibVprintf(fmt, args);
	va_end(args);
	#endif
}

void* alloc(size_t size) {
	SceUID memuid;
	void *addr;
	size = ALIGN(size, 4 * 1024);
	memuid = sceKernelAllocMemBlock("mem", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
	if (memuid < 0)
		return NULL;
	if (sceKernelGetMemBlockBase(memuid, &addr) < 0)
		return NULL;
	return addr;
}

void graphicsFree(SceUID uid) {
	void *mem;
	if (sceKernelGetMemBlockBase(uid, &mem) < 0)
		return NULL;
	if (sceGxmUnmapMemory(mem) < 0)
		return NULL;
	if (sceKernelFreeMemBlock(uid) < 0)
		return NULL;
}

void* gpu_alloc_map(SceKernelMemBlockType type, SceGxmMemoryAttribFlags gpu_attrib, size_t size, SceUID *uid) {
	SceUID memuid;
	void *addr;
	if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
		size = ALIGN(size, 0x40000);
	else
		size = ALIGN(size, 0x1000);
	memuid = sceKernelAllocMemBlock("gpumem", type, size, NULL);
	if (memuid < 0)
		return NULL;
	if (sceKernelGetMemBlockBase(memuid, &addr) < 0)
		return NULL;
	if (sceGxmMapMemory(addr, size, gpu_attrib) < 0) {
		sceKernelFreeMemBlock(memuid);
		return NULL;
	}
	if (uid)
		*uid = memuid;
	return addr;
}

void freeJpegTexture(Jpeg_texture *texture) {
	if (texture) {
		printf("Fucking with textures\n");
		if (texture->gxm_rtgt) {
			sceGxmDestroyRenderTarget(texture->gxm_rtgt);
		}
		if(texture->depth_UID) {
			graphicsFree(texture->depth_UID);
		}
		if(texture->palette_UID) {
			graphicsFree(texture->palette_UID);
		}
		graphicsFree(texture->data_UID);
		graphicsFree(texture->textureUID);
		texture = NULL;
	}
}