#include "csc.h"

#include <psp2/jpeg.h>
#include <arm_neon.h>

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define CSC_FIX(x)	((int)(x * 1024 + 0.5))
#define SCE_NULL		((void *)0)
#define ROUND_UP(x, a)	((((unsigned int)x)+((a)-1u))&(~((a)-1u)))

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
	y_offset	= (uint16x4_t)vdup_lane_s16(s_cscCoef[matrix], 0);
	uv_lvs		= vdup_lane_s16(s_cscCoef[matrix], 1);
	y_coef		= vdup_lane_s16(s_cscCoef[matrix], 2);
	rgba.val[3]	= vdup_lane_u8((uint8x8_t)s_cscCoef[matrix], 6);
	uv_coef		= s_cscCoef[matrix + 1];

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
			} else {
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
	y_offset	= (uint16x4_t)vdup_lane_s16(s_cscCoef[matrix], 0);
	uv_lvs		= vdup_lane_s16(s_cscCoef[matrix], 1);
	y_coef		= vdup_lane_s16(s_cscCoef[matrix], 2);
	rgba.val[3]	= vdup_lane_u8((uint8x8_t)s_cscCoef[matrix], 6);
	uv_coef		= s_cscCoef[matrix + 1];

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
		} else {
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
		if (pLastU != SCE_NULL) {
			// restore U/V ptr to head of current line
			chroma2ndLineOffset = (height > 2)? chromaLineSize: 0;
			pU = pLastU;
			pV = pLastV;
			pLastU = SCE_NULL;
		} else {
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

	if (pRGBA == SCE_NULL || pYCbCr == SCE_NULL ||
		(((uintptr_t)pRGBA | (uintptr_t)pYCbCr) & 3u)) {
		return SCE_JPEG_ERROR_INVALID_POINTER;
	}
	if (colorOption != SCE_JPEG_PIXEL_RGBA8888) {
		return SCE_JPEG_ERROR_UNSUPPORT_COLORSPACE;
	}

	width	= (unsigned int)xysize >> 16;
	height	= xysize & 0xFFFF;
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

