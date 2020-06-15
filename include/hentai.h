#ifndef HENTAI_H
#define HENTAI_H

#include "common.h"
#include "jpeg.h"

typedef struct VertexV32T32 { float x, y, z, u, v;} VertexV32T32;

void hentaiInit(void);
void hentaiCleanup(void);
SceBool hentaiHasAllFramebuffers(void);
SceBool hentaiHentaiTargetIsFinished(void);
JpegDecStatus hentaiGetJpegStatus(void);
void hentaiGxmInit(SceGxmInitializeParams *newInitializationParams, const SceGxmInitializeParams *params);
int hentaiShaderPatcher(SceGxmShaderPatcher **shaderPatcher);
void hentaiNextFrameBufAddress(const SceDisplayFrameBuf *pParam, SceBool *next);
void hentaiAddColorSurface(SceGxmColorSurface *surface, void *data);
void hentaiCompareTargetAddress(const SceGxmRenderTarget *renderTarget, const SceGxmColorSurface *colorSurface);
void hentaiSetDisplayState(const SceGxmRenderTarget *renderTarget);
void hentaiDraw(SceGxmContext *context);
int hentaiRandomHentai(void);
int hentaiReset(void);

#endif