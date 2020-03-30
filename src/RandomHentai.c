#include <stdio.h>
#include <taihen.h>
#include <vitasdk.h>

#include "jpeg.h"
#include "draw.h"
#include "math_utils.h"

#define _DEBUG_

#define printf sceClibPrintf

#define FILENAME			"ux0:/data/randomhentai/saved/160479.jpg"

#define MAX_IMAGE_WIDTH		960
#define MAX_IMAGE_HEIGHT	544
#define FRAME_BUF_SIZE		(MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT * 4)

#define MAX_IMAGE_BUF_SIZE	(MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT * 3 / 2)

/*E Buffer size for JPEG file */
#define MAX_JPEG_BUF_SIZE	(MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT * 2)

/*E Buffer size for quantized coefficients (needed for progressive JPEG only) */
#define MAX_COEF_BUF_SIZE	0
/*#define MAX_COEF_BUF_SIZE	(MAX_IMAGE_BUF_SIZE * 2 + 256)*/

/*E Control structures */
static JpegDecCtrl	s_decCtrl;
SceDisplayFrameBuf photoBuf;

JpegDecStatus status = JPEG_DEC_IDLE;
void 				*s_indexData;
static SceUID hooks[3];
static tai_hook_ref_t hook_refs[3];
int num1 = 1;
int num2 = 1;
int num3 = 1;
int ret = 0;
int newBuffer = 0;

typedef struct {
	float		x, y, z, u, v;
} VertexV32T32;

// shader patcher
SceGxmShaderPatcher *patcher;

extern const SceGxmProgram	_binary_texture_v_gxp_start;
extern const SceGxmProgram	_binary_texture_f_gxp_start;

static const SceGxmProgram *const textureVertexProgramGxp       = &_binary_texture_v_gxp_start;
static const SceGxmProgram *const textureFragmentProgramGxp     = &_binary_texture_f_gxp_start;

static SceGxmShaderPatcherId  vertexProgramId;
static SceGxmShaderPatcherId  fragmentProgramId;
static SceGxmVertexProgram	   *vertexProgram;
static SceGxmFragmentProgram  *fragmentProgram;

static SceGxmTexture			texture;
static SceUID				   verticesUid;
static SceUID				   indicesUid;
static SceUID				   frameBufMemBlock;
static const SceGxmProgramParameter *paramPositionAttribute = NULL;
static const SceGxmProgramParameter *paramColorAttribute = NULL;
static const SceGxmProgramParameter *wvp = NULL;
static VertexV32T32 *vertices = NULL;
static SceUInt16	*indices = NULL;
float			ratioX, ratioY, minX, minY, maxX, maxY;
static matrix4x4 mvp;

void delay(int seconds){
	unsigned long int count=333333333,i,j;
	
	for(i=0;i<seconds;i++)
	    for(j=0;j<count;j++);
}

void *graphicsAlloc(SceKernelMemBlockType type, unsigned int size, unsigned int alignment, unsigned int attribs, SceUID *uid)
{
	int ret = 0;
	void *mem = NULL;

	(void)ret;

	/*E page align the size */
	if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW) {
		/*E CDRAM memblocks must be 256KiB aligned */
		size = ROUND_UP(size, 0x40000);
	} else {
		/*E LPDDR memblocks must be 4KiB aligned */
		size = ROUND_UP(size, 0x1000);
	}

	*uid = sceKernelAllocMemBlock("vdispGraphics", type, size, NULL);
	if (ret < 0) {
		printf("sceKernelAllocMemBlock() failed\n");
	}

	ret = sceKernelGetMemBlockBase(*uid, &mem);
	if (ret != 0) {
		printf("sceKernelGetMemBlockBase() failed, ret 0x%x\n", ret);
	}

	ret = sceGxmMapMemory(mem, size, attribs);
	if (ret != 0) {
		printf("sceGxmMapMemory() failed, ret 0x%x\n", ret);
	}

	return mem;
}

void graphicsFree(SceUID uid)
{
	int ret = 0;
	void *mem = NULL;

	(void)ret;

	ret = sceKernelGetMemBlockBase(uid, &mem);
	if (ret != 0) {
		printf("sceKernelGetMemBlockBase() failed, ret 0x%x\n", ret);
	}

	ret = sceGxmUnmapMemory(mem);
	if (ret != 0) {
		printf("sceGxmUnmapMemory() failed, ret 0x%x\n", ret);
	}

	ret = sceKernelFreeMemBlock(uid);
	if (ret != 0) {
		printf("sceKernelFreeMemBlock() failed, ret 0x%x\n", ret);
	}
}

int sceDisplaySetFrameBuf_hentaiTime(const SceDisplayFrameBuf *pParam, int sync) {
  	if (num1) {
		num1 = 0;
    	printf("test\n");
		printf("Resolution: %dx%d, Pitch: %d, Format: %d\n", pParam->width, pParam->height, pParam->pitch, pParam->pixelformat);
		ret = jpegdecDecode(&s_decCtrl, pParam, FILENAME);
		if (ret < 0) {
			printf("Failed to Decode JPEG\n");
			printf("jpegdecDecode() 0x%08x\n", ret);
		}
		// ret = jpegdecTerm(&s_decCtrl);
		// if (ret < 0) {
		// 	printf("Failed to Terminate JPEG Decoder\n");
		// 	printf("jpegdecTerm() 0x%08x\n", ret);
		// }
		status = JPEG_DEC_DECODED;
  	}
	// drawSetFrameBuf(pParam);
	// //if (status == JPEG_DEC_DECODED) {
	// //	drawPictureCenter(photoBuf.width, photoBuf.height, photoBuf.base);
	// //}
  	// drawString(0, 320, "Nice");
  	// drawRect(250, 320, 40, 40, RGB_RED);
	return TAI_CONTINUE(int, hook_refs[0], pParam, sync);
}

int sceGxmEndScene_hentaiTime(SceGxmContext *context, const SceGxmNotification *vertexNotification, const SceGxmNotification *fragmentNotification)
{	
	int ret;
	if (status == JPEG_DEC_DECODED) {
		/* Prepare vertices */

		// ratioX = (float)s_decCtrl.validWidth  / (float)960;
		// ratioY = (float)s_decCtrl.validHeight / (float)544;
		// if (ratioX > 1.0f || ratioY > 1.0f) {
		// 	if (ratioX < ratioY) {
		// 		ratioX = ratioY;
		// 	}
		// 	minX = -1.f * (float)s_decCtrl.validWidth  / (960  * ratioX);
		// 	minY = -1.f * (float)s_decCtrl.validHeight / (544 * ratioX);
		// 	maxX =  1.f * (float)s_decCtrl.validWidth  / (960  * ratioX);
		// 	maxY =  1.f * (float)s_decCtrl.validHeight / (544 * ratioX);
		// } else {
		// 	minX = -1.f * ratioX;
		// 	minY = -1.f * ratioY;
		// 	maxX =  1.f * ratioX;
		// 	maxY =  1.f * ratioY;
		// }

		minX = (float)(960 / 2) - (float)(s_decCtrl.validWidth / 2);
		minY = (float)(544 / 2) - (float)(s_decCtrl.validHeight / 2);
		maxX = minX + (float)s_decCtrl.validWidth;
		maxY = minY + (float)s_decCtrl.validHeight;

		vertices[0].x = minX;
		vertices[0].y = minY;
		vertices[0].z = 0.5f;
		vertices[0].u = 0.0f;
		vertices[0].v = 0.0f;

		vertices[1].x = maxX;
		vertices[1].y = minY;
		vertices[1].z = 0.5f;
		vertices[1].u = 1.0f;
		vertices[1].v = 0.0f;

		vertices[2].x = minX;
		vertices[2].y = maxY;
		vertices[2].z = 0.5f;
		vertices[2].u = 0.0f;
		vertices[2].v = 1.0f;

		vertices[3].x = maxX;
		vertices[3].y = maxY;
		vertices[3].z = 0.5f;
		vertices[3].u = 1.0f;
		vertices[3].v = 1.0f;

		printf("1: X:%f, Y:%f, Z:%f\n2: X:%f, Y:%f, Z:%f\n3: X:%f, Y:%f, Z:%f\n4: X:%f, Y:%f, Z:%f\n", vertices[0].x, vertices[0].y, vertices[0].z , vertices[1].x, vertices[1].y, vertices[1].z , vertices[2].x, vertices[2].y, vertices[2].z , vertices[3].x, vertices[3].y, vertices[3].z );

		if (s_decCtrl.validWidth != photoBuf.width || s_decCtrl.validWidth & 7) {
			ret = sceGxmTextureInitLinearStrided(&texture, photoBuf.base,
				SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,
				s_decCtrl.validWidth, s_decCtrl.validHeight, photoBuf.width * 4);
			printf("Texture Init output: 0x%08x\n", ret);

			ret = sceGxmTextureSetMagFilter(&texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
			printf("Mag Filter output: 0x%08x\n", ret);

			printf("True\n");
		} else {
				sceGxmTextureInitLinear(&texture, photoBuf.base,
					SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,
					photoBuf.width, photoBuf.height, 0);
				sceGxmTextureSetMinFilter(&texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
				sceGxmTextureSetMagFilter(&texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
				sceGxmTextureSetUAddrMode(&texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
				sceGxmTextureSetVAddrMode(&texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
			printf("False\n");
		}

		ret = sceGxmTextureGetWidth(&texture);
		printf("Texture Width: %d\n", ret);

		/*validate Programs */
		ret = sceGxmProgramCheck(textureVertexProgramGxp);
		printf("Vertex Program Check output: 0x%08x\n", ret);
		ret = sceGxmProgramCheck(textureFragmentProgramGxp);
		printf("Fragment Program Check output: 0x%08x\n", ret);

		/*E set texture shaders */
		sceGxmSetVertexProgram(context, vertexProgram);
		sceGxmSetFragmentProgram(context, fragmentProgram);

		/*E draw the texture */
		void *vertex_wvp_buffer;

		ret = sceGxmReserveVertexDefaultUniformBuffer(context, &vertex_wvp_buffer);
		printf("Reserved Uniform Data output: %d\n", ret);

		ret = sceGxmSetUniformDataF(vertex_wvp_buffer, wvp, 0, 16, (const float*)mvp);
		printf("Uniform Data output: %d\n", ret);

		ret = sceGxmSetFragmentTexture(context, 0, &texture);
		printf("Fragment Texture output: %d\n", ret);

		ret = sceGxmSetVertexStream(context, 0, vertices);
		printf("Vertex Stream output: %d\n", ret);

		printf("Starting to Draw\n");

		ret = sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, indices, 4);
		printf("GxmDraw Output: %d\n", ret);

		printf("Finished Drawing\n");
	}
	return TAI_CONTINUE(int, hook_refs[1], context, vertexNotification, fragmentNotification);
}

int sceGxmShaderPatcherCreate_hentaiTime(const SceGxmShaderPatcherParams *params, SceGxmShaderPatcher **shaderPatcher)
{
	int res;
	printf("Starting Shader Patcher Patch\n");

	int ret = TAI_CONTINUE(int, hook_refs[2], params, shaderPatcher);
	// Grabbing a reference to used shader patcher
	patcher = *shaderPatcher;
	printf("Aquired Shader Patcher\n");

	res = sceGxmShaderPatcherRegisterProgram(patcher, textureVertexProgramGxp, &vertexProgramId);
	printf("Shader Patcher Register Vertex Out: 0x%08x\n", ret);

	res = sceGxmShaderPatcherRegisterProgram(patcher, textureFragmentProgramGxp, &fragmentProgramId);
	printf("Shader Patcher Register Fragment Out: 0x%08x\n", ret);

	printf("Registered Programs\n");

	paramPositionAttribute = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "aPosition");
	if (paramPositionAttribute == NULL) {
		printf("Couldn't Find Position Attribute!");
	}

	paramColorAttribute = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "aTexcoord");
	if (paramColorAttribute == NULL) {
		printf("Couldn't Find Texture Attribute!");
	}

	wvp = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "wvp");
	if (wvp == NULL) {
		printf("Couldn't Find WVP!");
	}

	printf("Aquired Atrributes\n");

	SceGxmVertexAttribute vertexAttributes[2];
	SceGxmVertexStream vertexStream[1];
	vertexAttributes[0].streamIndex = 0;
	vertexAttributes[0].offset = 0;
	vertexAttributes[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
	vertexAttributes[0].componentCount = 3;
	vertexAttributes[0].regIndex = sceGxmProgramParameterGetResourceIndex(paramPositionAttribute);
	vertexAttributes[1].streamIndex = 0;
	vertexAttributes[1].offset = 12;
	vertexAttributes[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
	vertexAttributes[1].componentCount = 2;
	vertexAttributes[1].regIndex = sceGxmProgramParameterGetResourceIndex(paramColorAttribute);
	vertexStream[0].stride = sizeof(VertexV32T32);
	vertexStream[0].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

	res = sceGxmShaderPatcherCreateVertexProgram(
		patcher,
		vertexProgramId,
		vertexAttributes, 2,
		vertexStream, 1,
		&vertexProgram);
	printf("Patcher Create Vertex Out: 0x%08x\n", ret);

	res = sceGxmShaderPatcherCreateFragmentProgram(
		patcher,
		fragmentProgramId,
		SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
		SCE_GXM_MULTISAMPLE_NONE,
		NULL,
		textureFragmentProgramGxp,
		&fragmentProgram);
	printf("Patcher Create Fragment Out: 0x%08x\n", ret);

	vertices = (VertexV32T32 *)graphicsAlloc(
		SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		4 * sizeof(VertexV32T32), 4,
		SCE_GXM_MEMORY_ATTRIB_READ, &verticesUid);

	indices = (SceUInt16 *)graphicsAlloc(
		SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		4 * sizeof(SceUInt16), 2,
		SCE_GXM_MEMORY_ATTRIB_READ, &indicesUid);;

	vertices[0] = (VertexV32T32){-1.0f, -1.0f,  0.0f,  0.0f,  1.0f};
	vertices[1] = (VertexV32T32){ 1.0f, -1.0f,  0.0f,  1.0f,  1.0f};
	vertices[2] = (VertexV32T32){-1.0f,  1.0f,  0.0f,  0.0f,  0.0f};
	vertices[3] = (VertexV32T32){ 1.0f,  1.0f,  0.0f,  1.0f,  0.0f};
	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 3;

	matrix4x4 projection, modelview;
	matrix4x4_identity(modelview);
	matrix4x4_init_orthographic(projection, 0, 960, 544, 0, -1, 1);
	matrix4x4_multiply(mvp, projection, modelview);


	SceSize totalBufSize = ROUND_UP(FRAME_BUF_SIZE * 2, 1024 * 1024);

	photoBuf.base = graphicsAlloc(
		SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
		totalBufSize,
		SCE_GXM_TEXTURE_ALIGNMENT,
		SCE_GXM_MEMORY_ATTRIB_READ,
		&frameBufMemBlock);
	
	photoBuf.base = (void*)((char*)photoBuf.base + 0 * DISP_FRAME_BUF_SIZE);

	printf("Initialized Verteces and Indices\n");

	return ret;
}

void _start() __attribute__ ((weak, alias ("module_start")));

int module_start(SceSize argc, const void *args) {

	printf("JPEG Buf: %d, Dec Buf: %d, Coef Buf: %d\n", MAX_JPEG_BUF_SIZE, MAX_IMAGE_BUF_SIZE, MAX_COEF_BUF_SIZE);

	ret = jpegdecInit(&s_decCtrl, &photoBuf, MAX_JPEG_BUF_SIZE, MAX_IMAGE_BUF_SIZE, MAX_COEF_BUF_SIZE);
	if (ret < 0) {
		printf("Failed to Initialize JPEG Decoder\n");
		status = JPEG_DEC_NO_INIT;
	} else {
		printf("JPEG Decoder Initialized\n");
	}

	hooks[0] = taiHookFunctionImport(&hook_refs[0], TAI_MAIN_MODULE, TAI_ANY_LIBRARY, 0x7A410B64, sceDisplaySetFrameBuf_hentaiTime);
	
	hooks[1] = taiHookFunctionImport(&hook_refs[1], TAI_MAIN_MODULE, TAI_ANY_LIBRARY, 0xFE300E2F /* 0x8734FF4E */, sceGxmEndScene_hentaiTime);

	hooks[2] = taiHookFunctionImport(&hook_refs[2], TAI_MAIN_MODULE, TAI_ANY_LIBRARY, 0x05032658, sceGxmShaderPatcherCreate_hentaiTime);
						
	return SCE_KERNEL_START_SUCCESS;
}
