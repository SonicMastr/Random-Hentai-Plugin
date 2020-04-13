#ifndef JPEG_H
#define JPEG_H

#include <psp2/gxm.h>
#include <psp2/types.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/pgf.h>
#include <psp2/pvf.h>

#define ROUND_UP(x, a)	((((unsigned int)x)+((a)-1u))&(~((a)-1u)))
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define printf sceClibPrintf

typedef enum JpegDecStatus {
	JPEG_DEC_IDLE,
	JPEG_DEC_DECODED,
	JPEG_DEC_DECODING,
	JPEG_DEC_NO_INIT,
	JPEG_DEC_ERROR,
} JpegDecStatus;

typedef struct Jpeg_texture {
	SceGxmTexture gxm_tex;
	SceUID data_UID;
	SceUID palette_UID;
	SceGxmRenderTarget *gxm_rtgt;
	SceGxmColorSurface gxm_sfc;
	SceGxmDepthStencilSurface gxm_sfd;
	SceUID depth_UID;
	SceInt validHeight;
	SceInt validWidth;
} Jpeg_texture;

void rh_JPEG_decoder_initialize(void);
void rh_JPEG_decoder_finish(void);
Jpeg_texture *rh_load_JPEG_file(const char *filename);

#endif