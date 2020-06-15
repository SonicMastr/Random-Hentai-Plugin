#include <stdio.h>
#include <string.h>
#include <arm_neon.h>
#include <psp2/io/fcntl.h>
#include <psp2/jpeg.h>
#include "jpeg.h"
#include "common.h"
#include <malloc.h>
#include <psp2/io/stat.h>

#define GXM_TEX_MAX_SIZE 4096
#define ROUND_UP(x, a)	((((unsigned int)x)+((a)-1u))&(~((a)-1u)))
#define MAX_IMAGE_WIDTH		960
#define MAX_IMAGE_HEIGHT	544
#define MAX_IMAGE_BUF_SIZE	(MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT * 3)
#define MAX_JPEG_BUF_SIZE	(MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT * 2)
#define MAX_COEF_BUF_SIZE	0
#define JPEG_TOTAL_BUFF_SIZE 2097152 + 1024

/*#define MAX_COEF_BUF_SIZE	(MAX_IMAGE_BUF_SIZE * 2 + 256)*/

#define VDISP_FRAME_WIDTH		960
#define VDISP_FRAME_HEIGHT		544
#define VDISP_FRAME_BUF_SIZE	(VDISP_FRAME_WIDTH * VDISP_FRAME_HEIGHT * 4)
#define VDISP_FRAME_BUF_NUM		2

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define CSC_FIX(x)	((int)(x * 1024 + 0.5))

extern void* sceClibMemset(void* s, int c, SceSize n);
extern void* gpu_alloc_map(SceKernelMemBlockType type, SceGxmMemoryAttribFlags gpu_attrib, size_t size, SceUID *uid);

extern void* sceClibMspaceMalloc(void* space, unsigned int size);
extern void sceClibMspaceFree(void* space, void* address);


void* mspace_internal;

typedef struct {
	SceUID		bufferMemBlock;
	void	   *pBuffer;
	SceSize		streamBufSize;
	SceSize		decodeBufSize;
	SceSize		coefBufSize;
} JpegDecCtrl;

typedef struct {
	int		 validWidth;
	int			validHeight;
	int			pitchWidth;
	int			pitchHeight;
} FrameInfo;

static JpegDecCtrl	s_decCtrl;
static SceSize totalBufSize;
static SceUID	textureUid;

/* CSC coefficients for ITU-R BT.601 full-range */
static const int16x4_t s_cscCoef[] = {
	/* y_offset, uv_lvs, y0, alpha */
	{ 0, -128, CSC_FIX(1.000), 255 },
	/* v0, u1, v1, u2 */
	{ CSC_FIX(1.402), -CSC_FIX(0.344), -CSC_FIX(0.714), CSC_FIX(1.772) }
};

/* chroma selector for bilinear upsampling */
static const uint8x8_t s_selBlChroma[] = {
	{  2, 32,  2, 32,  3, 32,  3, 32 }, // x3 - current samples
	{  2, 32,  3, 32,  2, 32,  4, 32 }, // x1 - left edge
	{  1, 32,  3, 32,  2, 32,  4, 32 }, //      middle
	{  1, 32,  3, 32,  2, 32,  3, 32 }  //      right edge
};

__attribute__((noinline))
static void yuv422ToRgba8888(
	uint8_t * __restrict__ pRGBA,
	const uint8_t * __restrict__ pY,
	const uint8_t * __restrict__ pU,
	const uint8_t * __restrict__ pV,
	unsigned int width, unsigned int height,
	unsigned int chromaPitchDiff, unsigned int pitchDiff
)
{
	int16x8_t sq16_temp;
	int16x4_t y0, u, v, next_y0, next_u, next_v;
	int32x4_t y1;
	uint16x4_t y_offset;
	int16x4_t uv_lvs, y_coef, uv_coef;
	int16x4_t r0, g0, b0, r1, g1, b1;
	uint8x8x4_t rgba;
	uint8x8_t u8_temp0, u8_temp1, u8_temp2;
	int16x4_t s16_temp0, s16_temp1;
	uint8x8_t selChromaMain, selChromaSubL, selChromaSubR;
	int x, matrix = 0;

	pitchDiff <<= 2;
	y_offset = (uint16x4_t)vdup_lane_s16(s_cscCoef[matrix], 0);
	uv_lvs = vdup_lane_s16(s_cscCoef[matrix], 1);
	y_coef = vdup_lane_s16(s_cscCoef[matrix], 2);
	rgba.val[3] = vdup_lane_u8((uint8x8_t)s_cscCoef[matrix], 6);
	uv_coef = s_cscCoef[matrix + 1];

	pU -= 2;
	pV -= 2;

	selChromaMain = s_selBlChroma[0];
	do {
		x = width;
		selChromaSubL = s_selBlChroma[1];
		do {
			// load
			u8_temp2 = vld1_u8(pY);
			u8_temp0 = vld1_u8(pU);
			u8_temp1 = vld1_u8(pV);
			pY += 8;
			pU += 4;
			pV += 4;
			sq16_temp = (int16x8_t)vmovl_u8(u8_temp2);
			y0 = vget_low_s16(sq16_temp);
			next_y0 = vget_high_s16(sq16_temp);
			y0 = (int16x4_t)vqsub_u16((uint16x4_t)y0, y_offset);
			next_y0 = (int16x4_t)vqsub_u16((uint16x4_t)next_y0, y_offset);
			if (likely(x > 8)) {
				selChromaSubR = s_selBlChroma[2];
			}
			else {
				selChromaSubR = s_selBlChroma[3];
			}
			// upsample U
			s16_temp0 = (int16x4_t)vtbl1_u8(u8_temp0, selChromaMain);
			s16_temp1 = (int16x4_t)vtbl1_u8(u8_temp0, selChromaSubL);
			s16_temp1 = vmla_n_s16(s16_temp1, s16_temp0, 3);
			u = vrshr_n_s16(s16_temp1, 2);
			u8_temp0 = (uint8x8_t)vshr_n_u64((uint64x1_t)u8_temp0, 16);
			s16_temp0 = (int16x4_t)vtbl1_u8(u8_temp0, selChromaMain);
			s16_temp1 = (int16x4_t)vtbl1_u8(u8_temp0, selChromaSubR);
			s16_temp1 = vmla_n_s16(s16_temp1, s16_temp0, 3);
			next_u = vrshr_n_s16(s16_temp1, 2);
			// upsample V
			s16_temp0 = (int16x4_t)vtbl1_u8(u8_temp1, selChromaMain);
			s16_temp1 = (int16x4_t)vtbl1_u8(u8_temp1, selChromaSubL);
			s16_temp1 = vmla_n_s16(s16_temp1, s16_temp0, 3);
			v = vrshr_n_s16(s16_temp1, 2);
			u8_temp1 = (uint8x8_t)vshr_n_u64((uint64x1_t)u8_temp1, 16);
			s16_temp0 = (int16x4_t)vtbl1_u8(u8_temp1, selChromaMain);
			s16_temp1 = (int16x4_t)vtbl1_u8(u8_temp1, selChromaSubR);
			s16_temp1 = vmla_n_s16(s16_temp1, s16_temp0, 3);
			next_v = vrshr_n_s16(s16_temp1, 2);
			// CSC
			y1 = vmull_s16(y0, y_coef);
			u = vadd_s16(u, uv_lvs);
			v = vadd_s16(v, uv_lvs);
			r0 = vqrshrn_n_s32(vmlal_lane_s16(y1, v, uv_coef, 0), 10);
			g0 = vqrshrn_n_s32(vmlal_lane_s16(vmlal_lane_s16(y1, u, uv_coef, 1), v, uv_coef, 2), 10);
			b0 = vqrshrn_n_s32(vmlal_lane_s16(y1, u, uv_coef, 3), 10);
			y1 = vmull_s16(next_y0, y_coef);
			u = vadd_s16(next_u, uv_lvs);
			v = vadd_s16(next_v, uv_lvs);
			r1 = vqrshrn_n_s32(vmlal_lane_s16(y1, v, uv_coef, 0), 10);
			g1 = vqrshrn_n_s32(vmlal_lane_s16(vmlal_lane_s16(y1, u, uv_coef, 1), v, uv_coef, 2), 10);
			b1 = vqrshrn_n_s32(vmlal_lane_s16(y1, u, uv_coef, 3), 10);
			// store
			rgba.val[0] = vqmovun_s16(vcombine_s16(r0, r1));
			rgba.val[1] = vqmovun_s16(vcombine_s16(g0, g1));
			rgba.val[2] = vqmovun_s16(vcombine_s16(b0, b1));
			vst4_u8(pRGBA, rgba);
			x -= 8;
			pRGBA += 4 * 8;
			selChromaSubL = s_selBlChroma[2];
		} while (x > 0);
		pU += chromaPitchDiff;
		pV += chromaPitchDiff;
		pRGBA += pitchDiff;
	} while (--height);

	return;
}

__attribute__((noinline))
static void yuv420ToRgba8888(
	uint8_t * __restrict__ pRGBA,
	const uint8_t * __restrict__ pY,
	const uint8_t * __restrict__ pU,
	const uint8_t * __restrict__ pV,
	unsigned int width, unsigned int height,
	unsigned int chromaPitchDiff, unsigned int pitchDiff
)
{
	int16x8_t sq16_temp;
	int16x4_t y0, u, v, next_y0, next_u, next_v;
	int32x4_t y1;
	uint16x4_t y_offset;
	int16x4_t uv_lvs, y_coef, uv_coef;
	int16x4_t r0, g0, b0, r1, g1, b1;
	uint8x8x4_t rgba;
	const uint8_t * __restrict__ pLastU;
	const uint8_t * __restrict__ pLastV;
	uint8x8_t u8_temp0, u8_temp1, u8_temp2, u8_temp3, u8_temp4;
	int16x4_t s16_temp0, s16_temp1, s16_temp2, s16_temp3;
	uint8x8_t selChromaMain, selChromaSubL, selChromaSubR;
	intptr_t chromaLineSize, chroma2ndLineOffset;
	int x, matrix = 0;

	pitchDiff <<= 2;
	y_offset = (uint16x4_t)vdup_lane_s16(s_cscCoef[matrix], 0);
	uv_lvs = vdup_lane_s16(s_cscCoef[matrix], 1);
	y_coef = vdup_lane_s16(s_cscCoef[matrix], 2);
	rgba.val[3] = vdup_lane_u8((uint8x8_t)s_cscCoef[matrix], 6);
	uv_coef = s_cscCoef[matrix + 1];

	pU -= 2;
	pV -= 2;

	chromaLineSize = (width >> 1) + chromaPitchDiff;
	chroma2ndLineOffset = 0;
	pLastU = pU;
	pLastV = pV;
	selChromaMain = s_selBlChroma[0];
	do {
		// load
		u8_temp4 = vld1_u8(pY);
		u8_temp0 = vld1_u8(pU);
		u8_temp1 = vld1_u8(pU + chroma2ndLineOffset);
		u8_temp2 = vld1_u8(pV);
		u8_temp3 = vld1_u8(pV + chroma2ndLineOffset);
		pY += 8;
		pU += 4;
		pV += 4;
		x = width;
		selChromaSubL = s_selBlChroma[1];
		if (likely(x > 8)) {
			selChromaSubR = s_selBlChroma[2];
		}
		else {
			selChromaSubR = s_selBlChroma[3];
		}
		do {
			sq16_temp = (int16x8_t)vmovl_u8(u8_temp4);
			y0 = vget_low_s16(sq16_temp);
			next_y0 = vget_high_s16(sq16_temp);
			y0 = (int16x4_t)vqsub_u16((uint16x4_t)y0, y_offset);
			next_y0 = (int16x4_t)vqsub_u16((uint16x4_t)next_y0, y_offset);
			// upsample U
			s16_temp1 = (int16x4_t)vtbl1_u8(u8_temp0, selChromaSubL);
			s16_temp2 = (int16x4_t)vtbl1_u8(u8_temp1, selChromaMain);
			s16_temp0 = (int16x4_t)vtbl1_u8(u8_temp0, selChromaMain);
			s16_temp3 = (int16x4_t)vtbl1_u8(u8_temp1, selChromaSubL);
			u8_temp0 = (uint8x8_t)vshr_n_u64((uint64x1_t)u8_temp0, 16);
			u8_temp1 = (uint8x8_t)vshr_n_u64((uint64x1_t)u8_temp1, 16);
			s16_temp1 = vadd_s16(s16_temp1, s16_temp2);
			s16_temp3 = vmla_n_s16(s16_temp3, s16_temp0, 9);
			s16_temp3 = vmla_n_s16(s16_temp3, s16_temp1, 3);
			u = vrshr_n_s16(s16_temp3, 4);
			s16_temp1 = (int16x4_t)vtbl1_u8(u8_temp0, selChromaSubR);
			s16_temp2 = (int16x4_t)vtbl1_u8(u8_temp1, selChromaMain);
			s16_temp0 = (int16x4_t)vtbl1_u8(u8_temp0, selChromaMain);
			s16_temp3 = (int16x4_t)vtbl1_u8(u8_temp1, selChromaSubR);
			s16_temp1 = vadd_s16(s16_temp1, s16_temp2);
			s16_temp3 = vmla_n_s16(s16_temp3, s16_temp0, 9);
			s16_temp3 = vmla_n_s16(s16_temp3, s16_temp1, 3);
			next_u = vrshr_n_s16(s16_temp3, 4);
			// upsample V
			s16_temp1 = (int16x4_t)vtbl1_u8(u8_temp2, selChromaSubL);
			s16_temp2 = (int16x4_t)vtbl1_u8(u8_temp3, selChromaMain);
			s16_temp0 = (int16x4_t)vtbl1_u8(u8_temp2, selChromaMain);
			s16_temp3 = (int16x4_t)vtbl1_u8(u8_temp3, selChromaSubL);
			u8_temp2 = (uint8x8_t)vshr_n_u64((uint64x1_t)u8_temp2, 16);
			u8_temp3 = (uint8x8_t)vshr_n_u64((uint64x1_t)u8_temp3, 16);
			s16_temp1 = vadd_s16(s16_temp1, s16_temp2);
			s16_temp3 = vmla_n_s16(s16_temp3, s16_temp0, 9);
			s16_temp3 = vmla_n_s16(s16_temp3, s16_temp1, 3);
			v = vrshr_n_s16(s16_temp3, 4);
			s16_temp1 = (int16x4_t)vtbl1_u8(u8_temp2, selChromaSubR);
			s16_temp2 = (int16x4_t)vtbl1_u8(u8_temp3, selChromaMain);
			s16_temp0 = (int16x4_t)vtbl1_u8(u8_temp2, selChromaMain);
			s16_temp3 = (int16x4_t)vtbl1_u8(u8_temp3, selChromaSubR);
			s16_temp1 = vadd_s16(s16_temp1, s16_temp2);
			s16_temp3 = vmla_n_s16(s16_temp3, s16_temp0, 9);
			s16_temp3 = vmla_n_s16(s16_temp3, s16_temp1, 3);
			next_v = vrshr_n_s16(s16_temp3, 4);
			selChromaSubL = selChromaSubR;
			// CSC
			y1 = vmull_s16(y0, y_coef);
			u = vadd_s16(u, uv_lvs);
			v = vadd_s16(v, uv_lvs);
			r0 = vqrshrn_n_s32(vmlal_lane_s16(y1, v, uv_coef, 0), 10);
			g0 = vqrshrn_n_s32(vmlal_lane_s16(vmlal_lane_s16(y1, u, uv_coef, 1), v, uv_coef, 2), 10);
			b0 = vqrshrn_n_s32(vmlal_lane_s16(y1, u, uv_coef, 3), 10);
			y1 = vmull_s16(next_y0, y_coef);
			u = vadd_s16(next_u, uv_lvs);
			v = vadd_s16(next_v, uv_lvs);
			r1 = vqrshrn_n_s32(vmlal_lane_s16(y1, v, uv_coef, 0), 10);
			g1 = vqrshrn_n_s32(vmlal_lane_s16(vmlal_lane_s16(y1, u, uv_coef, 1), v, uv_coef, 2), 10);
			// store
			rgba.val[0] = vqmovun_s16(vcombine_s16(r0, r1));
			b1 = vqrshrn_n_s32(vmlal_lane_s16(y1, u, uv_coef, 3), 10);
			rgba.val[1] = vqmovun_s16(vcombine_s16(g0, g1));
			rgba.val[2] = vqmovun_s16(vcombine_s16(b0, b1));
			// load
			if (likely(x > 8)) {
				u8_temp4 = vld1_u8(pY);
				u8_temp0 = vld1_u8(pU);
				u8_temp1 = vld1_u8(pU + chroma2ndLineOffset);
				u8_temp2 = vld1_u8(pV);
				u8_temp3 = vld1_u8(pV + chroma2ndLineOffset);
				pY += 8;
				pU += 4;
				pV += 4;
			}
			vst4_u8(pRGBA, rgba);
			x -= 8;
			pRGBA += 4 * 8;
			if (unlikely(x <= 8)) {
				selChromaSubR = s_selBlChroma[3];
			}
		} while (x > 0);
		if (pLastU != NULL) {
			// restore U/V ptr to head of current line
			chroma2ndLineOffset = (height > 2) ? chromaLineSize : 0;
			pU = pLastU;
			pV = pLastV;
			pLastU = NULL;
		}
		else {
			// forward U/V ptr to next line
			chroma2ndLineOffset = -chromaLineSize;
			pU += chromaPitchDiff;
			pV += chromaPitchDiff;
			pLastU = pU;
			pLastV = pV;
		}
		pRGBA += pitchDiff;
	} while (--height);

	return;
}

int csc(void *pRGBA, const unsigned char *pYCbCr, int xysize, int iFrameWidth,
	int colorOption, int sampling)
{
	unsigned int width, height, chromaPitchDiff, pitchDiff, ySize, cSize;

	if (pRGBA == NULL || pYCbCr == NULL ||
		(((uintptr_t)pRGBA | (uintptr_t)pYCbCr) & 3u)) {
		return SCE_JPEG_ERROR_INVALID_POINTER;
	}
	if (colorOption != SCE_JPEG_PIXEL_RGBA8888) {
		return SCE_JPEG_ERROR_UNSUPPORT_COLORSPACE;
	}

	width = (unsigned int)xysize >> 16;
	height = xysize & 0xFFFF;
	if (width == 0 || height == 0 || (width & 7u) || (unsigned int)iFrameWidth < width) {
		return SCE_JPEG_ERROR_UNSUPPORT_IMAGE_SIZE;
	}

	chromaPitchDiff = (width & 16) >> 1;
	pitchDiff = iFrameWidth - width;
	ySize = width * height;
	switch (sampling & 0xFFFF) {
	case SCE_JPEG_CS_H2V1:
		cSize = ROUND_UP(width, 32) * height >> 1;
		yuv422ToRgba8888(
			(uint8_t * __restrict__)pRGBA,
			(const uint8_t * __restrict__)pYCbCr,
			(const uint8_t * __restrict__)(pYCbCr + ySize),
			(const uint8_t * __restrict__)(pYCbCr + ySize + cSize),
			width, height, chromaPitchDiff, pitchDiff);
		break;
	case SCE_JPEG_CS_H2V2:
		if ((height & 1u)) {
			return SCE_JPEG_ERROR_UNSUPPORT_IMAGE_SIZE;
		}
		cSize = ROUND_UP(width, 32) * height >> 2;
		yuv420ToRgba8888(
			(uint8_t * __restrict__)pRGBA,
			(const uint8_t * __restrict__)pYCbCr,
			(const uint8_t * __restrict__)(pYCbCr + ySize),
			(const uint8_t * __restrict__)(pYCbCr + ySize + cSize),
			width, height, chromaPitchDiff, pitchDiff);
		break;
	default:
		return SCE_JPEG_ERROR_UNSUPPORT_SAMPLING;
	}

	return 0;
}

int readFile(const char *fileName, unsigned char *pBuffer, SceSize bufSize)
{
	int ret;
	SceIoStat stat;
	SceUID fd;
	int remainSize;

	ret = sceIoGetstat(fileName, &stat);
	fd = sceIoOpen(fileName, SCE_O_RDONLY, 0);
	if (fd < 0) {
		printf("sceIoOpen(%s) 0x%08x\n", fileName, ret);
		return -1;
	}
	remainSize = (SceSize)stat.st_size;
	if (stat.st_size > bufSize) {
		printf("file too large (bufSize %d byte)\n", bufSize);
		return -1;
	}
	while (remainSize > 0) {
		ret = sceIoRead(fd, pBuffer, remainSize);
		pBuffer += ret;
		remainSize -= ret;
	}
	ret = sceIoClose(fd);

	return (int)stat.st_size;
}

int jpegdecInit(SceSize streamBufSize, SceSize decodeBufSize, SceSize coefBufSize)
{
	SceJpegMJpegInitParam initParam;

	SceKernelFreeMemorySizeInfo meminfo;
	meminfo.size = sizeof(meminfo);

	// Defaults for Memory Allocation
	SceKernelMemBlockType memBlockType = SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW;
	SceSize memBlockAlign = 1024 * 1024;
 
	sceKernelGetFreeMemorySize(&meminfo);
	if (meminfo.size_phycont >= JPEG_TOTAL_BUFF_SIZE) {
		initParam.option = SCE_JPEG_MJPEG_INIT_OPTION_LPDDR2_MEMORY;
		printf("Chose PHYCONT\n");
	} else if (meminfo.size_cdram >= JPEG_TOTAL_BUFF_SIZE) {
		memBlockType = SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
		memBlockAlign = 256 * 1024;
		initParam.option = SCE_JPEG_MJPEG_INIT_OPTION_NONE;
		printf("Chose CDRAM\n");
	} else if (meminfo.size_user >= JPEG_TOTAL_BUFF_SIZE) {
		memBlockType = SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE;
		initParam.option = SCE_JPEG_MJPEG_INIT_OPTION_LPDDR2_MEMORY;
		printf("Chose USER\n");
	} else {
		printf("Not enough memory (2MB) to initialize Jpeg Decoder\nPHYCONT: %d\nCDRAM: %d\nUSER: %d\n", meminfo.size_phycont, meminfo.size_cdram, meminfo.size_user);
		return -1;
	}
	printf("PHYCONT: %d\nCDRAM: %d\nUSER: %d\n", meminfo.size_phycont, meminfo.size_cdram, meminfo.size_user);

	streamBufSize = ROUND_UP(streamBufSize, 256);
	decodeBufSize = ROUND_UP(decodeBufSize, 256);
	coefBufSize = ROUND_UP(coefBufSize, 256);
	totalBufSize = ROUND_UP(streamBufSize + decodeBufSize + coefBufSize, memBlockAlign);

	s_decCtrl.bufferMemBlock = sceKernelAllocMemBlock("jpegdecBuffer",
		memBlockType, totalBufSize, NULL);
		
	if (s_decCtrl.bufferMemBlock < 0) {
		printf("sceKernelAllocMemBlock() 0x%08x\n", s_decCtrl.bufferMemBlock);
		return s_decCtrl.bufferMemBlock;
	}

	int ret = sceKernelGetMemBlockBase(s_decCtrl.bufferMemBlock, &s_decCtrl.pBuffer);
	if (ret < 0) {
		printf("sceKernelGetMemBlockBase() 0x%08x\n", ret);
		return ret;
	}
	printf("sceKernelGetMemBlockBase() 0x%08x\n", ret);

	/*E Initialize JPEG decoder. */
	initParam.size = sizeof(SceJpegMJpegInitParam);
	initParam.maxSplitDecoder = 0;
	initParam.option = SCE_JPEG_MJPEG_INIT_OPTION_LPDDR2_MEMORY;
	ret = sceJpegInitMJpegWithParam(&initParam);
	printf("sceJpegInitMJpegWithParam() 0x%08x\n", ret);

	s_decCtrl.streamBufSize = streamBufSize;
	s_decCtrl.decodeBufSize = decodeBufSize;
	s_decCtrl.coefBufSize = coefBufSize;

	printf("Initialized Jpeg Decoder with decode buffer size of %d bytes\ntotal buffer size of %d bytes\n", s_decCtrl.decodeBufSize, totalBufSize);

	return 0;
}

int jpegdecTerm(void)
{
	int ret;

	ret = sceJpegFinishMJpeg();
	if (s_decCtrl.bufferMemBlock >= 0) {
		ret |= sceKernelFreeMemBlock(s_decCtrl.bufferMemBlock);
	}

	return 0;
}

int rh_JPEG_decoder_initialize(void)
{
	int ret = jpegdecInit(MAX_JPEG_BUF_SIZE, MAX_IMAGE_BUF_SIZE, MAX_COEF_BUF_SIZE);
	if (ret != 0) {
		printf("Failed to init JPEG Decoder\n");
	}
	return ret;
}

void rh_JPEG_decoder_finish(void)
{
	jpegdecTerm();
}

Jpeg_texture *rh_load_JPEG_file(const char *filename)
{
	int ret;
	SceJpegOutputInfo outputInfo;
	unsigned char *pJpeg = (unsigned char*)s_decCtrl.pBuffer;
	SceSize isize;
	unsigned char *pYCbCr;
	void *pCoefBuffer;
	int decodeMode = SCE_JPEG_MJPEG_WITH_DHT;
	int validWidth, validHeight;

	printf("Reading File\n");
	/*E Read JPEG file to buffer. */
	ret = readFile(filename, pJpeg, s_decCtrl.streamBufSize);
	if (ret < 0) {
		return NULL;
	}
	isize = ret;
	printf("Loaded File of Size %d Bytes\n", isize);

	printf("Getting JPEG File Information\n");
	/*E Get JPEG output information. */
	ret = sceJpegGetOutputInfo(pJpeg, isize,
		SCE_JPEG_NO_CSC_OUTPUT, decodeMode, &outputInfo);
	if (ret == SCE_JPEG_ERROR_UNSUPPORT_SAMPLING &&
		outputInfo.colorSpace == (SCE_JPEG_CS_YCBCR | SCE_JPEG_CS_H1V1)) {
		/* Set SCE_JPEG_MJPEG_ANY_SAMPLING for decodeMode and retry sceJpegGetOutputInfo(),
		   if the JPEG's color space is YCbCr 4:4:4. */
		decodeMode = SCE_JPEG_MJPEG_ANY_SAMPLING;
		ret = sceJpegGetOutputInfo(pJpeg, isize,
			SCE_JPEG_NO_CSC_OUTPUT, decodeMode, &outputInfo);
	}
	if (ret < 0) {
		printf("sceJpegGetOutputInfo() 0x%08x\n", ret);
		return NULL;
	}
	printf("Got JPEG File Information\n");
	printf("colorSpace       0x%08x\n", outputInfo.colorSpace);
	printf("imageSize        %u x %u\n", outputInfo.imageWidth, outputInfo.imageHeight);
	printf("outputBufferSize %u byte\n", outputInfo.outputBufferSize);
	printf("coefBufferSize   %u byte\n", outputInfo.coefBufferSize);

	printf("Calculating Downscale Ratio\n");
	/*E Calculate downscale ratio. */
	{
		float downScaleWidth, downScaleHeight, downScale;
		int downScaleDiv;

		downScaleWidth = (float)outputInfo.pitch[0].x / 960;
		downScaleHeight = (float)outputInfo.pitch[0].y / 544;
		downScale = (downScaleWidth >= downScaleHeight) ? downScaleWidth : downScaleHeight;
		if (downScale <= 1.f) {
			/*E Downscale is not needed. */
		}
		else if (downScale <= 2.f) {
			decodeMode |= SCE_JPEG_MJPEG_DOWNSCALE_1_2;
		}
		else if (downScale <= 4.f) {
			decodeMode |= SCE_JPEG_MJPEG_DOWNSCALE_1_4;
		}
		else if (downScale <= 5.f) {
			decodeMode |= SCE_JPEG_MJPEG_DOWNSCALE_1_8;
		} else {
			printf("Too big image size, can't downscale to at least %u x %u\n",
				960, 544);
			return NULL;
		}
		downScaleDiv = (decodeMode >> 3) & 0xe;
		if (downScaleDiv) {
			printf("downScale        1 / %u\n", downScaleDiv);
			validWidth = (outputInfo.imageWidth + downScaleDiv - 1) / downScaleDiv;
			validHeight = (outputInfo.imageHeight + downScaleDiv - 1) / downScaleDiv;
		}
		else {
			printf("downScale        none\n");
			validWidth = outputInfo.imageWidth;
			validHeight = outputInfo.imageHeight;
		}
		printf("Valid Width: %d, Valid Height: %d\n", validWidth, validHeight);

	}
	printf("Calculated Downscale Ratio\n");

	printf("Setting Buffers\n");
	/*E Set output buffer and quantized coefficients buffer. */
	pYCbCr = pJpeg + s_decCtrl.streamBufSize;
	if (outputInfo.coefBufferSize > 0 && s_decCtrl.coefBufSize > 0) {
		printf("Set pCoefBuffer\n");
		pCoefBuffer = (void*)(pYCbCr + s_decCtrl.decodeBufSize);
	}
	else {
		pCoefBuffer = NULL;
	}
	printf("Set Buffers\n");

	printf("Decoding JPEG Stream\n");
	/*E Decode JPEG stream */
	ret = sceJpegDecodeMJpegYCbCr(
		pJpeg, isize,
		pYCbCr, s_decCtrl.decodeBufSize, decodeMode,
		pCoefBuffer, s_decCtrl.coefBufSize);
	if (ret < 0) {
		printf("sceJpegDecodeMJpegYCbCr() 0x%08x\n", ret);
		return NULL;
	}

	FrameInfo pFrameInfo;

	pFrameInfo.pitchWidth = ret >> 16;
	printf("Pitch Width: %d\n", pFrameInfo.pitchWidth);
	pFrameInfo.pitchHeight = ret & 0xFFFF;
	printf("Pitch Height: %d\n", pFrameInfo.pitchHeight);
	pFrameInfo.validWidth = validWidth;
	pFrameInfo.validHeight = validHeight;

	if (pFrameInfo.pitchWidth > GXM_TEX_MAX_SIZE || pFrameInfo.pitchHeight > GXM_TEX_MAX_SIZE)
		return NULL;
	
	printf("Decoded JPEG Stream\n");

	//return NULL;

	unsigned int size = ROUND_UP(4 * 1024 * pFrameInfo.validHeight, 1024 * 1024);

	printf("Allocating Texture Memory\n");
	SceUID tex_data_uid = sceKernelAllocMemBlock("cdlg_mem", 0x0CA08060 /* SCE_KERNEL_MEMBLOCK_TYPE_USER_UNKNOWN_RW_UNCACHE  It's really part of CDIALOG memory, but seems to have a way smaller size*/, size, NULL);

	void* texture_data;
	printf("Checking Texture Memory Base\n");
	if (sceKernelGetMemBlockBase(tex_data_uid, &texture_data) < 0)
		return NULL;

	printf("Mapping Texture Memory\n");
	if (sceGxmMapMemory(texture_data, size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE) < 0)
		return NULL;

	printf("Clearing Texture Data\n");
	/* Clear the texture */
	sceClibMemset(texture_data, 0, size);
	printf("Texture Data Cleared\n");

	printf("CSC crap. Let it do its thing\n");
	//E CSC (YCbCr -> RGBA) 
	if ((decodeMode & 3) == SCE_JPEG_MJPEG_WITH_DHT) {
		if (pFrameInfo.pitchWidth >= 64 && pFrameInfo.pitchHeight >= 64) {
			//E YCbCr 4:2:0 or YCbCr 4:2:2 (fast, processed on dedicated hardware) 
			ret = sceJpegMJpegCsc(
				texture_data, pYCbCr, ret, pFrameInfo.pitchWidth,
				SCE_JPEG_PIXEL_RGBA8888, outputInfo.colorSpace & 0xFFFF);
		}
		else {
			//E YCbCr 4:2:0 or YCbCr 4:2:2, image width < 64 or height < 64
				//(slow, processed on the CPU) 
			ret = csc(
				texture_data, pYCbCr, ret, pFrameInfo.pitchWidth,
				SCE_JPEG_PIXEL_RGBA8888, outputInfo.colorSpace & 0xFFFF);
		}
	}
	else {
		//E YCbCr 4:4:4 (slow, processed on the codec engine) 
		ret = sceJpegCsc(
			texture_data, pYCbCr, ret, pFrameInfo.pitchWidth,
			SCE_JPEG_PIXEL_RGBA8888, outputInfo.colorSpace & 0xFFFF);
	}
	printf("Setting Texture Information\n");

	Jpeg_texture *texture = gpu_alloc_map(
		SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SCE_GXM_MEMORY_ATTRIB_READ,
		4 * sizeof(*texture), &textureUid);

	printf("Clearing the Decoder Buffer\n");
	/* Clear the decoder buffer */
	sceClibMemset(s_decCtrl.pBuffer, 0, totalBufSize);
	printf("Decoder Buffer Cleared\n");

	printf("Set Texture Data UID\n");
	texture->data_UID = tex_data_uid;
	texture->textureUID = textureUid;

	texture->validHeight = validHeight;
	texture->validWidth = validWidth;

	printf("Create GXM Texture\n");
	/* Create the gxm texture */
	ret = sceGxmTextureInitLinear(
		&texture->gxm_tex,
		texture_data,
		SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR,
		pFrameInfo.pitchWidth,
		pFrameInfo.pitchHeight,
		0);

	printf("Texture Init Out: 0x%08x\n", ret);
	
	printf("Set Palette UID\n");
	texture->palette_UID = 0;

	printf("Done!\n");
	return texture;
}