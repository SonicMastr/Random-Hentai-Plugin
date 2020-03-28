#ifndef _JPEG_H_
#define _JPEG_H_

#include <psp2/kernel/sysmem.h>
#include <psp2/display.h>
#include <psp2/types.h>

#define ROUND_UP(x, a)	((((unsigned int)x)+((a)-1u))&(~((a)-1u)))
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

/*E frame buffer */
#define DISP_FRAME_WIDTH		960
#define DISP_FRAME_HEIGHT		544
#define DISP_FRAME_BUF_SIZE	(DISP_FRAME_WIDTH * DISP_FRAME_HEIGHT * 4)
#define DISP_FRAME_BUF_NUM		2

/*E display buffer */
#define DISP_DISPLAY_WIDTH		960
#define DISP_DISPLAY_HEIGHT	544
#define DISP_DISPLAY_PITCH		1024
#define DISP_DISPLAY_BUF_SIZE \
	ROUND_UP(DISP_DISPLAY_PITCH * DISP_DISPLAY_HEIGHT * 4, 1024 * 1024)
#define DISP_DISPLAY_BUF_NUM	2
#define DISP_DISPLAY_FLIP_INTERVAL	2

typedef enum JpegDecStatus {
	JPEG_DEC_DECODED,
	JPEG_DEC_DECODING,
	JPEG_DEC_NO_INIT,
	JPEG_DEC_ERROR,
} JpegDecStatus;

typedef struct {
	int			validWidth;
	int			validHeight;
	int			pitchWidth;
	int			pitchHeight;
} JpegDispFrameInfo;

typedef struct {
	SceUID		bufferMemBlock;
	SceDisplayFrameBuf *photoBuf;
	void	   *pBuffer;
	void	   *pFrameBuf;
	SceSize		streamBufSize;
	SceSize		decodeBufSize;
	SceSize		coefBufSize;
	JpegDispFrameInfo *photoBufInfo;
} JpegDecCtrl;

int jpegdecInit(JpegDecCtrl *pCtrl, SceDisplayFrameBuf *photoBuf, SceSize streamBufSize, SceSize decodeBufSize, SceSize coefBufSize);

int jpegdecDecode(const SceDisplayFrameBuf *pParam, SceSize streamBufSize, SceSize decodeBufSize, SceSize coefBufSize, const char* fileName);

#endif
