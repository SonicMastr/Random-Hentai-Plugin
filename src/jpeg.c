#include <psp2/io/stat.h> 
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h> 
#include <psp2/jpeg.h>

#include "jpeg.h"
#include "csc.h"
#include "draw.h"

#define printf sceClibPrintf

#define SCE_NULL		((void *)0)

static SceUID fb_uid = -1;

int jpegdecInit(JpegDecCtrl *pCtrl, SceDisplayFrameBuf *photoBuf, SceSize streamBufSize, SceSize decodeBufSize, SceSize coefBufSize)
{
	int ret = 0;
	SceSize totalBufSize;
	SceJpegMJpegInitParam initParam;
    SceKernelMemBlockType memBlockType = SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
	SceSize memBlockAlign = 256 * 1024;
	/*E Allocate buffers for JPEG decoder. */
	streamBufSize = ROUND_UP(streamBufSize, 256);
	decodeBufSize = ROUND_UP(decodeBufSize, 256);
	coefBufSize = ROUND_UP(coefBufSize, 256);
	totalBufSize = ROUND_UP(streamBufSize + decodeBufSize + coefBufSize, memBlockAlign);

	pCtrl->bufferMemBlock = sceKernelAllocMemBlock("jpegdecBuffer",
		memBlockType, totalBufSize, SCE_NULL);
	if (pCtrl->bufferMemBlock < 0) {
		printf("sceKernelAllocMemBlock() 0x%08x\n", pCtrl->bufferMemBlock);
		return pCtrl->bufferMemBlock;
	}
	ret = sceKernelGetMemBlockBase(pCtrl->bufferMemBlock, &pCtrl->pBuffer);
	if (ret < 0) {
		printf("sceKernelGetMemBlockBase() 0x%08x\n", ret);
		return ret;
	}

	/* Initialize JPEG decoder. */
	initParam.size				= sizeof(SceJpegMJpegInitParam);
	initParam.maxSplitDecoder	= 0;
	initParam.option			= SCE_JPEG_MJPEG_INIT_OPTION_NONE;
	ret = sceJpegInitMJpegWithParam(&initParam);
	if (ret < 0) {
		printf("sceJpegInitMJpegWithParam() 0x%08x\n", ret);
		return ret;
	}

	pCtrl->photoBuf = photoBuf;
	pCtrl->streamBufSize = streamBufSize;
	pCtrl->decodeBufSize = decodeBufSize;
	pCtrl->coefBufSize = coefBufSize;
	return ret;
}

int jpegdecDecode(const SceDisplayFrameBuf *pParam, SceSize streamBufSize, SceSize decodeBufSize, SceSize coefBufSize, const char* fileName)
{
    int ret = 0;
	SceSize totalBufSize;
	SceJpegMJpegInitParam initParam;
    SceKernelMemBlockType memBlockType = SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
	SceSize memBlockAlign = 256 * 1024;

    SceUID		bufferMemBlock;
	void	   *pBuffer;

    SceJpegOutputInfo outputInfo;
    SceSize isize;
	unsigned char *pYCbCr;
	void *pCoefBuffer;
	int decodeMode = SCE_JPEG_MJPEG_WITH_DHT;
	int validWidth, validHeight, pitchWidth, pitchHeight;

    static SceInt pwidth, pheight, bufferwidth, pixelformat;
    static SceSize *vram32;

    streamBufSize = ROUND_UP(streamBufSize, 256);
	decodeBufSize = ROUND_UP(decodeBufSize, 256);
	coefBufSize = ROUND_UP(coefBufSize, 256);
	totalBufSize = ROUND_UP(streamBufSize + decodeBufSize + coefBufSize, memBlockAlign);

    bufferMemBlock = sceKernelAllocMemBlock("jpegdecBuffer",
		memBlockType, totalBufSize, SCE_NULL);
	if (bufferMemBlock < 0) {
		printf("sceKernelAllocMemBlock() 0x%08x\n", bufferMemBlock);
		return bufferMemBlock;
	}
	ret = sceKernelGetMemBlockBase(bufferMemBlock, &pBuffer);
	if (ret < 0) {
		printf("sceKernelGetMemBlockBase() 0x%08x\n", ret);
		return ret;
	}

    unsigned char *pJpeg = (unsigned char*)pBuffer;

    /*E Initialize JPEG decoder. */
	initParam.size				= sizeof(SceJpegMJpegInitParam);
	initParam.maxSplitDecoder	= 0;
	initParam.option			= SCE_JPEG_MJPEG_INIT_OPTION_NONE;
	ret = sceJpegInitMJpegWithParam(&initParam);
	if (ret < 0) {
		printf("sceJpegInitMJpegWithParam() 0x%08x\n", ret);
		return ret;
	}

    /* Decode JPEG Start */
	/*E Read JPEG file to buffer. */
	ret = readFile(fileName, pJpeg, streamBufSize);
	if (ret < 0) {
		printf("readFile() 0x%08x\n", ret);
		return ret;
	}
	isize = ret;

    /*E Get JPEG output information. */
	ret = sceJpegGetOutputInfo(pJpeg, isize,
		SCE_JPEG_NO_CSC_OUTPUT, decodeMode, &outputInfo);
	if (ret == SCE_JPEG_ERROR_UNSUPPORT_SAMPLING &&
		outputInfo.colorSpace == (SCE_JPEG_CS_YCBCR | SCE_JPEG_CS_H1V1)) {
		/* Set SCE_JPEG_MJPEG_ANY_SAMPLING for decodeMode and retry sceJpegGetOutputInfo(),
		   if the JPEG's color space is YCbCr 4:4:4. */
		printf("Set SCE_JPEG_MJPEG_ANY_SAMPLING for decodeMode, "
		   "because the JPEG's color space is YCbCr 4:4:4.\n");
		decodeMode = SCE_JPEG_MJPEG_ANY_SAMPLING;
		ret = sceJpegGetOutputInfo(pJpeg, isize,
			SCE_JPEG_NO_CSC_OUTPUT, decodeMode, &outputInfo);
	}
	if (ret < 0) {
		printf("sceJpegGetOutputInfo() 0x%08x\n", ret);
		return ret;
	}
	printf("colorSpace       0x%08x\n", outputInfo.colorSpace);
	printf("imageSize        %u x %u\n", outputInfo.imageWidth, outputInfo.imageHeight);
	printf("outputBufferSize %u byte\n", outputInfo.outputBufferSize);
	printf("coefBufferSize   %u byte\n", outputInfo.coefBufferSize);

	/*E Calculate downscale ratio. */
	{
		float downScaleWidth, downScaleHeight, downScale;
		int downScaleDiv;

		downScaleWidth  = (float)outputInfo.pitch[0].x / DISP_FRAME_WIDTH;
		downScaleHeight = (float)outputInfo.pitch[0].y / DISP_FRAME_HEIGHT;
		downScale = (downScaleWidth >= downScaleHeight)? downScaleWidth: downScaleHeight;
		if (downScale <= 1.f) {
			/*E Downscale is not needed. */
		} else if (downScale <= 3.f) {
			decodeMode |= SCE_JPEG_MJPEG_DOWNSCALE_1_2;
		} else if (downScale <= 6.f) {
			decodeMode |= SCE_JPEG_MJPEG_DOWNSCALE_1_4;
		} else if (downScale <= 8.f) {
			decodeMode |= SCE_JPEG_MJPEG_DOWNSCALE_1_8;
		} else {
			printf("Too big image size, can't downscale to %u x %u\n",
				DISP_FRAME_WIDTH, DISP_FRAME_HEIGHT);
			return -1;
		}
		downScaleDiv = (decodeMode >> 3) & 0xe;
		if (downScaleDiv) {
			printf("downScale        1 / %u\n", downScaleDiv);
			validWidth  = (outputInfo.imageWidth  + downScaleDiv - 1) / downScaleDiv;
			validHeight = (outputInfo.imageHeight + downScaleDiv - 1) / downScaleDiv;
		} else {
			printf("downScale        none\n");
			validWidth  = outputInfo.imageWidth;
			validHeight = outputInfo.imageHeight;
		}
	}

    pwidth = pParam->width;
	pheight = pParam->height;
	vram32 = pParam->base;
	bufferwidth = pParam->pitch;
	pixelformat = pParam->pixelformat;

	if ((bufferwidth == 0) || (pixelformat != 0)) {
        printf("Could not get Framebuffer Information\n");
        return -1;
    }

    printf("Check Framebuffer Info...\nResolution: %dx%d, Pitch: %d, Format: %d\n", pwidth, pheight, bufferwidth, pixelformat);

    /*E Set output buffer and quantized coefficients buffer. */
	pYCbCr = pJpeg + streamBufSize;
	if (outputInfo.coefBufferSize > 0 && coefBufSize > 0) {
		pCoefBuffer = (void*)(pYCbCr + decodeBufSize);
	} else {
		pCoefBuffer = NULL;
	}

	/*E Decode JPEG stream */
	printf("Decoding JPEG stream\n");
	ret = sceJpegDecodeMJpegYCbCr(
		pJpeg, isize,
		pYCbCr, decodeBufSize, decodeMode,
		pCoefBuffer, coefBufSize);
	if (ret < 0) {
		printf("sceJpegDecodeMJpegYCbCr() 0x%08x\n", ret);
		return ret;
	}

    printf("Before Setting Frame Info\n");
	//if (pFrameInfo == NULL) {
	//	printf("vdispGetWriteBuffer() returned NULL\n");
	//	return -1;
	//}
	pitchWidth  = ret >> 16;
	pitchHeight = ret & 0xFFFF;

    printf("After Setting Frame Info\n");

    printf("%dx%d\n", pitchWidth, pitchHeight);

    unsigned int fb_size = ALIGN(4 * 1024 * pitchHeight, 256 * 1024);
 
    fb_uid = sceKernelAllocMemBlock("fb", 0x09408060 , fb_size, NULL);
 
    void *fb_addr = NULL;
    sceKernelGetMemBlockBase(fb_uid, &fb_addr);

	/*E CSC (YCbCr -> RGBA) */
	if ((decodeMode & 3) == SCE_JPEG_MJPEG_WITH_DHT) {
		if (pitchWidth >= 64 && pitchHeight >= 64) {
			/*E YCbCr 4:2:0 or YCbCr 4:2:2 (fast, processed on dedicated hardware) */
			ret = sceJpegMJpegCsc(
				fb_addr, pYCbCr, ret, pitchWidth,
				SCE_JPEG_PIXEL_RGBA8888, outputInfo.colorSpace & 0xFFFF);
			if (ret < 0) {
				printf("sceJpegMJpegCsc() 0x%08x\n", ret);
				return ret;
			}
		} else {
			/*E YCbCr 4:2:0 or YCbCr 4:2:2, image width < 64 or height < 64
				(slow, processed on the CPU) */
			ret = csc(
				fb_addr, pYCbCr, ret, pitchWidth,
				SCE_JPEG_PIXEL_RGBA8888, outputInfo.colorSpace & 0xFFFF);
			if (ret < 0) {
				printf("csc() 0x%08x\n", ret);
				return ret;
			}
		}
	} else {
		/*E YCbCr 4:4:4 (slow, processed on the codec engine) */
		ret = sceJpegCsc(
			fb_addr, pYCbCr, ret, pitchWidth,
			SCE_JPEG_PIXEL_RGBA8888, outputInfo.colorSpace & 0xFFFF);
		if (ret < 0) {
			printf("sceJpegCsc() 0x%08x\n", ret);
			return ret;
		}
	}

	/*E Display frame buffer */
    drawSetFrameBuf(pParam);
    printf("set draw frame");
    drawPictureCenter(pitchWidth,pitchHeight,fb_addr);

    return 0;
}

int readFile(const char *fileName, unsigned char *pBuffer, SceSize bufSize)
{
	int ret;
	SceIoStat stat;
	SceUID fd;
	int remainSize;

	ret = sceIoGetstat(fileName, &stat);
	if (ret < 0) {
		printf("sceIoGetstat(%s) 0x%08x\n", fileName, ret);
		return ret;
	}
	printf("%s, fileSize %lld byte\n", fileName, stat.st_size);
	if (stat.st_size > bufSize) {
		printf("file too large (bufSize %d byte)\n", bufSize);
		return -1;
	}
	fd = sceIoOpen(fileName, SCE_O_RDONLY, 0);
	if (fd < 0) {
		printf("sceIoOpen(%s) 0x%08x\n", fileName, ret);
		return fd;
	}
	remainSize = (SceSize)stat.st_size;
	while (remainSize > 0) {
		ret = sceIoRead(fd, pBuffer, remainSize);
		if (ret < 0 || ret > remainSize) {
			printf("sceIoRead() 0x%08x\n", ret);
			sceIoClose(fd);
			return -1;
		}
		pBuffer += ret;
		remainSize -= ret;
	}
	ret = sceIoClose(fd);
	if (ret < 0) {
		printf("sceIoClose() 0x%08x\n", ret);
		return ret;
	}
	return (int)stat.st_size;
}