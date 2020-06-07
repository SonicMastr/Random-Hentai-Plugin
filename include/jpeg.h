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

typedef struct SceSharedFbInfo { // size is 0x58
	void* base1;		// cdram base
	int memsize;
	void* base2;		// cdram base
	int unk_0C;
	void *unk_10;
	int unk_14;
	int unk_18;
	int unk_1C;
	int unk_20;
	int unk_24;		// 960
	int unk_28;		// 960
	int unk_2C;		// 544
	int unk_30;
	int curbuf;
	int unk_38;
	int unk_3C;
	int unk_40;
	int unk_44;
	int vsync;
	int unk_4C;
	int unk_50;
	int unk_54;
} SceSharedFbInfo;

typedef enum JpegDecStatus {
	JPEG_DEC_IDLE,
	JPEG_DEC_WAITING,
	JPEG_DEC_DECODED,
	JPEG_DEC_DECODING,
	JPEG_DEC_DISPLAY,
	JPEG_DEC_NO_INIT,
	JPEG_DEC_DIAG_TERM,
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
	SceUID textureUID;
	SceInt validHeight;
	SceInt validWidth;
} Jpeg_texture;

int rh_JPEG_decoder_initialize(void);
void rh_JPEG_decoder_finish(void);
Jpeg_texture *rh_load_JPEG_file(const char *filename, SceDisplayFrameBuf *bframe);

#endif