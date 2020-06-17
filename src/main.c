#include <stdio.h>
#include <stdlib.h>
#include <taihen.h>
#include <psp2/ctrl.h>
#include <math.h>
#include "common.h"
#include "hentai.h"
#include "dir.h"

#define TICK 10000      // This isn't a literal tick. I can already see people commenting on this.
#define SECOND 1000000
#define MINUTE 60000000
#define HOOK_NUM 6
static SceUID hook[HOOK_NUM];
static tai_hook_ref_t hook_ref[HOOK_NUM];

SceBool sceCommonDialogIsRunning(void);

static SceUID HOOK(int id, uint32_t nid, const void *func){
	hook[id] = taiHookFunctionImport(hook_ref+id,TAI_MAIN_MODULE,TAI_ANY_LIBRARY,nid,func);
	return hook[id];
}

static int hentai_thread(SceSize args, void *argp) {
	/* Random Number Generator Variables */
	SceUInt randomNumber;
	int max = 15;
	int min = 2;
	/* Control Variable */
	SceCtrlData ctrl;

	/* Main Loop */
	while(1) {
		while (hentaiHentaiTargetIsFinished()) {
			switch (hentaiGetJpegStatus()) {
				case JPEG_DEC_WAITING:
					sceKernelGetRandomNumber(&randomNumber, 4);
					sceKernelDelayThread(((randomNumber % (max-min+1)) + min)*SECOND);
					if (sceCommonDialogIsRunning())
						break;
					hentaiRandomHentai();
					break;
				case JPEG_DEC_DISPLAY:
				case JPEG_DEC_DECODED:
					sceCtrlPeekBufferPositive(0, &ctrl, 1);
					if (ctrl.buttons == (SCE_CTRL_LTRIGGER | SCE_CTRL_CIRCLE))
						hentaiReset();
					break;
				case JPEG_DEC_ERROR:
					hentaiReset();
					break;
				default:
					break;
			}
			sceKernelDelayThread(TICK);
		}
		sceKernelDelayThread(TICK);
	}
	return 0;
}

int sceGxmInitialize_henTime(const SceGxmInitializeParams *params) {	
	SceGxmInitializeParams initializeParams;
	hentaiGxmInit(&initializeParams, params);
	return TAI_CONTINUE(int, hook_ref[0], &initializeParams);
}

int sceGxmShaderPatcherCreate_henTime(const SceGxmShaderPatcherParams *params, SceGxmShaderPatcher **shaderPatcher)
{
	GLZ(TAI_CONTINUE(int, hook_ref[1], params, shaderPatcher));
	return hentaiShaderPatcher(shaderPatcher);
}

int sceDisplaySetFrameBuf_henTime(const SceDisplayFrameBuf *pParam, SceDisplaySetBufSync sync) {
	static SceBool next = SCE_TRUE;
	if (next)
		hentaiNextFrameBufAddress(pParam, &next);
	return TAI_CONTINUE(int, hook_ref[2], pParam, sync);
}

int sceGxmColorSurfaceInit_henTime(SceGxmColorSurface *surface, SceGxmColorFormat colorFormat,
	SceGxmColorSurfaceType surfaceType, SceGxmColorSurfaceScaleMode scaleMode,
	SceGxmOutputRegisterSize outputRegisterSize, unsigned int width, 
	unsigned int height, unsigned int strideInPixels, void *data) {
	hentaiAddColorSurface(surface, data);
	return TAI_CONTINUE(int, hook_ref[3], surface, colorFormat, surfaceType, scaleMode, outputRegisterSize, width, height, strideInPixels, data);
}

int sceGxmBeginScene_henTime(SceGxmContext *context, unsigned int flags, 
	const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion,
	SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject, 
    const SceGxmColorSurface *colorSurface, const SceGxmDepthStencilSurface *depthStencil ) {
	
	if (hentaiHasAllFramebuffers()) {
		if (hentaiHentaiTargetIsFinished()) {
			hentaiSetDisplayState(renderTarget);
		} else {
			hentaiCompareTargetAddress(renderTarget, colorSurface);
		}
	}
	
	return TAI_CONTINUE(int, hook_ref[4], context, flags, renderTarget, validRegion, vertexSyncObject, fragmentSyncObject, colorSurface, depthStencil);
}

int sceGxmEndScene_henTime(SceGxmContext *context, const SceGxmNotification *vertexNotification,
    const SceGxmNotification *fragmentNotification) {
	hentaiDraw(context);
	return TAI_CONTINUE(int, hook_ref[5], context, vertexNotification, fragmentNotification);
}

static void initializeMem(void) {
	readDir();
	hentaiInit();
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {

	initializeMem();
	HOOK(0, 0xB0F1E4EC, sceGxmInitialize_henTime);
	HOOK(1, 0x05032658, sceGxmShaderPatcherCreate_henTime);
	HOOK(2, 0x7A410B64, sceDisplaySetFrameBuf_henTime);
	HOOK(3, 0xED0F6E25, sceGxmColorSurfaceInit_henTime);
	HOOK(4, 0x8734FF4E, sceGxmBeginScene_henTime);
	HOOK(5, 0xFE300E2F, sceGxmEndScene_henTime);

	SceUID thid;
	thid = sceKernelCreateThread("hentai_thread", hentai_thread, 0x60, 0x10000, 0, 0, NULL);
	if (thid >= 0)
		sceKernelStartThread(thid, 0, NULL); 
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
	int i;
	hentaiCleanup();
	while (i < HOOK_NUM){
		taiHookRelease(hook[i], hook_ref[i]);
		i++;
	}
	return SCE_KERNEL_STOP_SUCCESS;	
}
