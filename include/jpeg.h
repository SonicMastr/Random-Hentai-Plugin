#ifndef JPEG_H
#define JPEG_H

#include "common.h"

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
Jpeg_texture *rh_load_JPEG_file(const char *filename);

#endif