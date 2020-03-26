#include <stdio.h>
#include <taihen.h>
#include <vitasdk.h>

#include "jpeg.h"
#include "draw.h"

#define printf sceClibPrintf

#define FILENAME			"ux0:/data/randomhentai/saved/53354.jpg"

#define MAX_IMAGE_WIDTH		960
#define MAX_IMAGE_HEIGHT	544

#define MAX_IMAGE_BUF_SIZE	(MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT * 3 / 2)

/*E Buffer size for JPEG file */
#define MAX_JPEG_BUF_SIZE	(MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT)

/*E Buffer size for quantized coefficients (needed for progressive JPEG only) */
#define MAX_COEF_BUF_SIZE	0
/*#define MAX_COEF_BUF_SIZE	(MAX_IMAGE_BUF_SIZE * 2 + 256)*/

/*E Control structures */
SceDisplayFrameBuf *newBuf;
SceDisplayFrameBuf *oldBuf;

static SceUID hook;
static tai_hook_ref_t display_ref;
int num = 1;
int ret = 0;
int newBuffer = 0;

int sceDisplaySetFrameBuf_hentaiTime(const SceDisplayFrameBuf *pParam, int sync) {
  	if (num) {
		num = 0;
    	printf("test\n");
		printf("Resolution: %dx%d, Pitch: %d, Format: %d\n", pParam->width, pParam->height, pParam->pitch, pParam->pixelformat);
		ret = jpegdecDecode(pParam, MAX_JPEG_BUF_SIZE, MAX_IMAGE_BUF_SIZE, MAX_COEF_BUF_SIZE, FILENAME);
		if (ret < 0) {
			printf("Failed to Decode JPEG\n");
			printf("jpegdecDecode() 0x%08x\n", ret);
		}
		drawSetFrameBuf(pParam);
  			drawString(0, 320, "Nice");
  		drawRect(250, 320, 40, 40, RGB_RED);
		return TAI_CONTINUE(int, display_ref, pParam, sync);
  	}
	while(1) {
		printf("I had to do this Loop. Ignore this\n");
	}
}

void _start() __attribute__ ((weak, alias ("module_start")));

int module_start(SceSize argc, const void *args) {

	printf("JPEG Buf: %d, Dec Buf: %d, Coef Buf: %d\n", MAX_JPEG_BUF_SIZE, MAX_IMAGE_BUF_SIZE, MAX_COEF_BUF_SIZE);
	
	hook = taiHookFunctionImport(&display_ref,
						TAI_MAIN_MODULE,
						TAI_ANY_LIBRARY,
						0x7A410B64,
						sceDisplaySetFrameBuf_hentaiTime);
						
	return SCE_KERNEL_START_SUCCESS;
}
