#ifndef UTIL_H
#define UTIL_H

#include "common.h"
#include "jpeg.h"

void LOG(const char *fmt, ...);
void* alloc(size_t size);
void graphicsFree(SceUID uid);
void* gpu_alloc_map(SceKernelMemBlockType type, SceGxmMemoryAttribFlags gpu_attrib, size_t size, SceUID *uid);
void freeJpegTexture(Jpeg_texture *texture);

#endif