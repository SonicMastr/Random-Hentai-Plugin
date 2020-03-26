#ifndef _CSC_H_
#define _CSC_H_

#include <psp2/types.h>

int csc(void *pRGBA, const unsigned char *pYCbCr, int xysize, int iFrameWidth,
	int colorOption, int sampling);

#endif
