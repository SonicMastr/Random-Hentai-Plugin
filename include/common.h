#ifndef COMMON_H
#define COMMON_H

#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/types.h>
#include <psp2/kernel/rng.h> 
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>

#define ROUND_UP(x, a)	((((unsigned int)x)+((a)-1u))&(~((a)-1u)))
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define printf sceClibPrintf
/* Just return -1, It really isn't that necessary to check for everything*/
#define GLZ(x) do {\
	if ((x) < 0) { return -1; }\
} while (0)

typedef enum RenderTargetInitStatus {
	RENDERTARGET_IDLE,
	RENDERTARGET_FIRST_SCENE,
	RENDERTARGET_ALL_FRAMEBUFFERS,
	RENDERTARGET_FINISHED,
	RENDERTARGET_ERROR,
} RenderTargetInitStatus;

#endif