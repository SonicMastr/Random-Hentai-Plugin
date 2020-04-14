#include <stdio.h>
#include <taihen.h>
#include <vitasdk.h>

#include "jpeg.h"

#define FILENAME			"ux0:/data/randomhentai/saved/51418.jpg"

#define HOOKS_NUM 2

static uint8_t current_hook;
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t hook_refs[HOOKS_NUM];

// Set Decoder Status
JpegDecStatus status = JPEG_DEC_NO_INIT;
Jpeg_texture *texture;

// Define Vertex Values
typedef struct VertexV32T32 {
	float		x, y, z, u, v;
} VertexV32T32;

// Define Matrix
typedef float matrix4x4[4][4];

// shader patcher
SceGxmShaderPatcher *patcher;

extern const SceGxmProgram texture_v_gxp_start;
extern const SceGxmProgram texture_f_gxp_start;

static const SceGxmProgram *const textureVertexProgramGxp       = &texture_v_gxp_start;
static const SceGxmProgram *const textureFragmentProgramGxp     = &texture_f_gxp_start;

static SceGxmShaderPatcherId  vertexProgramId;
static SceGxmShaderPatcherId  fragmentProgramId;
static SceGxmVertexProgram	   *vertexProgram = NULL;
static SceGxmFragmentProgram  *fragmentProgram = NULL;

static inline int min(int a, int b) { return a < b ? a : b; }

static SceUID				   verticesUid;
static SceUID				   indicesUid;
static const SceGxmProgramParameter* paramPositionAttribute = NULL;
static const SceGxmProgramParameter* paramTextureAttribute = NULL;
static const SceGxmProgramParameter* wvp = NULL;
static VertexV32T32 *vertices = NULL;
static SceUInt16	*indices = NULL;
float			ratioX, ratioY, minX, minY, maxX, maxY;
float _vita2d_ortho_matrix[4*4];

void hookFunction(uint32_t nid, const void *func){
	hooks[current_hook] = taiHookFunctionImport(&hook_refs[current_hook],TAI_MAIN_MODULE,TAI_ANY_LIBRARY,nid,func);
	current_hook++;
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

int sceGxmShaderPatcherCreate_hentaiTime(const SceGxmShaderPatcherParams *params, SceGxmShaderPatcher **shaderPatcher)
{
	int res;
	printf("Starting Shader Patcher Patch\n");

	int ret = TAI_CONTINUE(int, hook_refs[0], params, shaderPatcher);
	printf("Ran Shader Patcher\n");
	// Grabbing a reference to used shader patcher
	patcher = *shaderPatcher;
	printf("Aquired Shader Patcher: 0x%08x\n", ret);

	res = sceGxmProgramCheck(textureVertexProgramGxp);
	printf("Check Vertex: 0x%08x\n", res);
	res = sceGxmProgramCheck(textureFragmentProgramGxp);;

	res = sceGxmShaderPatcherRegisterProgram(patcher, textureVertexProgramGxp, &vertexProgramId);
	printf("Check Fragment: 0x%08x\n", res);

	res = sceGxmShaderPatcherRegisterProgram(patcher, textureFragmentProgramGxp, &fragmentProgramId);
	printf("Shader Patcher Register Fragment Out: 0x%08x\n", res);

	printf("Registered Programs\n");

	paramPositionAttribute = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "aPosition");
	if (paramPositionAttribute == NULL) {
		printf("Couldn't Find Position Attribute!");
	}

	paramTextureAttribute = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "aTexcoord");
	if (paramTextureAttribute == NULL) {
		printf("Couldn't Find Texture Attribute!");
	}

	wvp = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "wvp");
	if (wvp == NULL) {
		printf("Couldn't Find WVP!");
	}

	printf("Aquired Atrributes\n");

	SceGxmVertexAttribute vertexAttributes[2];
	SceGxmVertexStream vertexStreams[1];
	vertexAttributes[0].streamIndex = 0;
	vertexAttributes[0].offset = 0;
	vertexAttributes[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
	vertexAttributes[0].componentCount = 3;
	vertexAttributes[0].regIndex = sceGxmProgramParameterGetResourceIndex(paramPositionAttribute);
	vertexAttributes[1].streamIndex = 0;
	vertexAttributes[1].offset = 12;
	vertexAttributes[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
	vertexAttributes[1].componentCount = 2;
	vertexAttributes[1].regIndex = sceGxmProgramParameterGetResourceIndex(paramTextureAttribute);
	vertexStreams[0].stride = sizeof(VertexV32T32);
	vertexStreams[0].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

	res = sceGxmShaderPatcherCreateVertexProgram(
		patcher,
		vertexProgramId,
		vertexAttributes, 2,
		vertexStreams, 1,
		&vertexProgram);
	printf("Patcher Create Vertex Out: 0x%08x\n", res);

	SceGxmBlendInfo blendInfo;

	blendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
	blendInfo.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
	blendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
	blendInfo.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
	blendInfo.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendInfo.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
	blendInfo.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;

	res = sceGxmShaderPatcherCreateFragmentProgram(
		patcher,
		fragmentProgramId,
		SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
		SCE_GXM_MULTISAMPLE_NONE,
		&blendInfo,
		textureVertexProgramGxp,
		&fragmentProgram);
	printf("Patcher Create Fragment Out: 0x%08x\n", res);

	vertices = gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SCE_GXM_MEMORY_ATTRIB_READ, 4 * sizeof(VertexV32T32), &verticesUid);

	indices = gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SCE_GXM_MEMORY_ATTRIB_READ, 4 * sizeof(unsigned short), &indicesUid);

	int i;
	for (i=0;i<4;i++){
		indices[i] = i;
	}

	matrix_init_orthographic(_vita2d_ortho_matrix, 0.0f, 960, 544, 0.0f, 0.0f, 1.0f);

	printf("Initialized Verteces and Indices\n");
	rh_JPEG_decoder_initialize();
	status = JPEG_DEC_DECODING;

	return ret;
}

int sceGxmEndScene_hentaiTime(SceGxmContext *context, const SceGxmNotification *vertexNotification, const SceGxmNotification *fragmentNotification)
{	
	int ret;
    if (status == JPEG_DEC_DECODING) {
        printf("Decoding Texture\n");
        texture = rh_load_JPEG_file(FILENAME);
        if (texture == NULL) {
            printf("Error Decoding Texture\n");
			rh_JPEG_decoder_finish();
            status = JPEG_DEC_ERROR;
            goto skip;
        }
        printf("Decoded Texture\n");
		rh_JPEG_decoder_finish();
        status = JPEG_DEC_DECODED;
    }
    if (status == JPEG_DEC_DECODED) {
		int width = texture->validWidth;
        int height = texture->validHeight;

		if (width > 960) {
			width = 960;
		}

		if (height > 544) {
			height = 544;
		}

        printf("%dx%d\n", width, height);


        minX = (float)(960 / 2) - (float)(width / 2);
        minY = (float)(544 / 2) - (float)(height / 2);
        maxX = minX + (float)width;
        maxY = minY + (float)height;

        vertices[0].x = minX;
        vertices[0].y = minY;
        vertices[0].z = 1.0f;
        vertices[0].u = 0.0f;
        vertices[0].v = 0.0f;

        vertices[1].x = maxX;
        vertices[1].y = minY;
        vertices[1].z = 1.0f;
        vertices[1].u = 1.0f;
        vertices[1].v = 0.0f;

        vertices[2].x = minX;
        vertices[2].y = maxY;
        vertices[2].z = 1.0f;
        vertices[2].u = 0.0f;
        vertices[2].v = 1.0f;

        vertices[3].x = maxX;
        vertices[3].y = maxY;
        vertices[3].z = 1.0f;
        vertices[3].u = 1.0f;
        vertices[3].v = 1.0f;

        printf("1: X:%f, Y:%f, Z:%f\n2: X:%f, Y:%f, Z:%f\n3: X:%f, Y:%f, Z:%f\n4: X:%f, Y:%f, Z:%f\n", vertices[0].x, vertices[0].y, vertices[0].z , vertices[1].x, vertices[1].y, vertices[1].z , vertices[2].x, vertices[2].y, vertices[2].z , vertices[3].x, vertices[3].y, vertices[3].z );

		/*E set texture */
		ret = sceGxmSetFragmentTexture(context, 0, &texture->gxm_tex);
        printf("Fragment Texture output: %d\n", ret);

        /*E set texture shaders */
        sceGxmSetVertexProgram(context, vertexProgram);
        sceGxmSetFragmentProgram(context, fragmentProgram);

        /*E draw the texture */

        void *vertex_wvp_buffer;

        ret = sceGxmReserveVertexDefaultUniformBuffer(context, &vertex_wvp_buffer);
        printf("Reserved Uniform Data output:0x%08x\n", ret);

        ret = sceGxmSetUniformDataF(vertex_wvp_buffer, wvp, 0, 16, _vita2d_ortho_matrix);
        printf("Uniform Data output: %d\n", ret);
		
        ret = sceGxmSetVertexStream(context, 0, vertices);
        printf("Vertex Stream output: %d\n", ret);

        printf("Starting to Draw\n");
        ret = sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, indices, 4);
        printf("GxmDraw Output: %d\n", ret);

        printf("Finished Drawing\n");
    }
    skip:
	ret = TAI_CONTINUE(int, hook_refs[1], context, vertexNotification, fragmentNotification);
	return ret;
}

void _start() __attribute__ ((weak, alias ("module_start")));

int module_start(SceSize argc, const void *args) {
    status = JPEG_DEC_IDLE;

	hookFunction(0x05032658, sceGxmShaderPatcherCreate_hentaiTime);
	printf("Initialized Hook 0\n");
	hookFunction(0xFE300E2F, sceGxmEndScene_hentaiTime);
	printf("Initialized Hook 1\n");			
	return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize argc, const void *args) {

	rh_JPEG_decoder_finish();
	status = JPEG_DEC_NO_INIT;
	if (vertices) {
		graphicsFree(verticesUid);
	}
	if (indices) {
		graphicsFree(indicesUid);
	}
	
	// Freeing hooks

	// Freeing hooks
	while (current_hook-- > 0){
		taiHookRelease(hooks[current_hook], hook_refs[current_hook]);
	}
	
	return SCE_KERNEL_STOP_SUCCESS;	
}