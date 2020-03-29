#include <stdio.h>
#include <taihen.h>
#include <vitasdk.h>

#include "jpeg.h"
#include "draw.h"

#define _DEBUG_

#define printf sceClibPrintf

#define FILENAME			"ux0:/data/randomhentai/saved/160479.jpg"

#define MAX_IMAGE_WIDTH		960
#define MAX_IMAGE_HEIGHT	544

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

typedef float matrix[4*4];

int *pindices;

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
static const SceGxmProgramParameter *paramPositionAttribute;
static const SceGxmProgramParameter *paramColorAttribute;
static const SceGxmProgramParameter *wvp;
static VertexV32T32	*vertices = NULL;
static SceUInt16		*indices = NULL;
float			ratioX, ratioY, minX, minY, maxX, maxY;
static matrix orthographic;

void delay(int seconds){
	unsigned long int count=333333333,i,j;
	
	for(i=0;i<seconds;i++)
	    for(j=0;j<count;j++);
}

void* gpu_alloc_map(SceKernelMemBlockType type, SceGxmMemoryAttribFlags gpu_attrib, size_t size, SceUID *uid){
	SceUID memuid;
	void *addr;

	if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
		size = ALIGN(size, 256 * 1024);
	else
		size = ALIGN(size, 4 * 1024);

	memuid = sceKernelAllocMemBlock("gpumem", type, size, NULL);
	if (memuid < 0)
		return NULL;

	if (sceKernelGetMemBlockBase(memuid, &addr) < 0)
		return NULL;

	if (sceGxmMapMemory(addr, size, gpu_attrib) < 0) {
		sceKernelFreeMemBlock(memuid);
		return NULL;
	}

	if (uid)
		*uid = memuid;

	return addr;
}

/* from Vita2d */
void matrix_init_orthographic(float *m, float left, float right, float bottom, float top, float near, float far)
{
	m[0x0] = 2.0f/(right-left);
	m[0x4] = 0.0f;
	m[0x8] = 0.0f;
	m[0xC] = -(right+left)/(right-left);

	m[0x1] = 0.0f;
	m[0x5] = 2.0f/(top-bottom);
	m[0x9] = 0.0f;
	m[0xD] = -(top+bottom)/(top-bottom);

	m[0x2] = 0.0f;
	m[0x6] = 0.0f;
	m[0xA] = -2.0f/(far-near);
	m[0xE] = (far+near)/(far-near);

	m[0x3] = 0.0f;
	m[0x7] = 0.0f;
	m[0xB] = 0.0f;
	m[0xF] = 1.0f;
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
		ratioX = (float)s_decCtrl.validWidth  / (float)960;
		ratioY = (float)s_decCtrl.validHeight / (float)544;
		if (ratioX > 1.0f || ratioY > 1.0f) {
			if (ratioX < ratioY) {
				ratioX = ratioY;
			}
			minX = -1.f * (float)s_decCtrl.validWidth  / (960  * ratioX);
			minY = -1.f * (float)s_decCtrl.validHeight / (544 * ratioX);
			maxX =  1.f * (float)s_decCtrl.validWidth  / (960  * ratioX);
			maxY =  1.f * (float)s_decCtrl.validHeight / (544 * ratioX);
		} else {
			minX = -1.f * ratioX;
			minY = -1.f * ratioY;
			maxX =  1.f * ratioX;
			maxY =  1.f * ratioY;
		}
		vertices[0].x = minX;
		vertices[0].y = minY;
		vertices[0].z = 1.f;
		vertices[1].x = maxX;
		vertices[1].y = minY;
		vertices[1].z = 1.f;
		vertices[2].x = minX;
		vertices[2].y = maxY;
		vertices[2].z = 1.f;
		vertices[3].x = maxX;
		vertices[3].y = maxY;
		vertices[3].z = 1.f;

		if (s_decCtrl.validWidth != photoBuf.width || s_decCtrl.validWidth & 7) {
			ret = sceGxmTextureInitLinearStrided(&texture, photoBuf.base,
				SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,
				s_decCtrl.validWidth, s_decCtrl.validHeight, photoBuf.width * 4);
			printf("Texture Init output: %d\n", ret);
			sceGxmTextureSetMagFilter(&texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
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

		/*E set texture shaders */
		sceGxmSetVertexProgram(context, vertexProgram);
		sceGxmSetFragmentProgram(context, fragmentProgram);

		/*E draw the texture */
		void *vertex_wvp_buffer;
		ret = sceGxmReserveVertexDefaultUniformBuffer(context, &vertex_wvp_buffer);
		printf("Reserved Uniform Data output: %d\n", ret);
		ret = sceGxmSetUniformDataF(vertex_wvp_buffer, wvp, 0, 16, orthographic);
		printf("Uniform Data output: %d\n", ret);
		ret = sceGxmSetFragmentTexture(context, 0, &texture);
		printf("Fragment Texture output: %d\n", ret);
		ret = sceGxmSetVertexStream(context, 0, vertices);
		printf("Vertex Stream output: %d\n", ret);
		printf("Starting to Draw\n");
		sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, indices, 4);
		printf("Finished Drawing\n");
	}
	return TAI_CONTINUE(int, hook_refs[1], context, vertexNotification, fragmentNotification);
}

int sceGxmShaderPatcherCreate_hentaiTime(const SceGxmShaderPatcherParams *params, SceGxmShaderPatcher **shaderPatcher)
{
	printf("Starting Shader Patcher Patch\n");

	int ret = TAI_CONTINUE(int, hook_refs[2], params, shaderPatcher);
	// Grabbing a reference to used shader patcher
	patcher = *shaderPatcher;

	printf("Aquired Shader Patcher\n");
	sceGxmShaderPatcherRegisterProgram(patcher, textureVertexProgramGxp, &vertexProgramId);
	sceGxmShaderPatcherRegisterProgram(patcher, textureFragmentProgramGxp, &fragmentProgramId);

	printf("Registered Programs\n");

	paramPositionAttribute = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "aPosition");

	paramColorAttribute = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "aTexcoord");

	wvp = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "wvp");

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

	sceGxmShaderPatcherCreateVertexProgram(
		patcher,
		vertexProgramId,
		vertexAttributes, 2,
		vertexStream, 1,
		&vertexProgram);

	sceGxmShaderPatcherCreateFragmentProgram(
		patcher,
		fragmentProgramId,
		SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
		SCE_GXM_MULTISAMPLE_NONE,
		NULL,
		textureFragmentProgramGxp,
		&fragmentProgram);

	vertices = gpu_alloc_map( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_MEMORY_ATTRIB_READ, 4 * sizeof(VertexV32T32), &verticesUid);
	indices = gpu_alloc_map( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_MEMORY_ATTRIB_READ, 4 * sizeof(unsigned short), &indicesUid);

	vertices[0] = (VertexV32T32){-1.0f, -1.0f,  0.0f,  0.0f,  1.0f};
	vertices[1] = (VertexV32T32){ 1.0f, -1.0f,  0.0f,  1.0f,  1.0f};
	vertices[2] = (VertexV32T32){-1.0f,  1.0f,  0.0f,  0.0f,  0.0f};
	vertices[3] = (VertexV32T32){ 1.0f,  1.0f,  0.0f,  1.0f,  0.0f};
	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 3;

	matrix_init_orthographic(orthographic, 0.0f, 960, 544, 0.0f, 0.0f, 1.0f);

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
