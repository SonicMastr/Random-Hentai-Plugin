#include <stdio.h>
#include <stdlib.h>
#include <taihen.h>
#include <vitasdk.h>
#include <math.h>

#include "jpeg.h"

#define FILENAME			"ux0:/data/randomhentai/saved/247566.jpg"

#define HOOKS_NUM 7

#define VDISP_FRAME_WIDTH		960
#define VDISP_FRAME_HEIGHT		544
#define VDISP_HALF_WIDTH 		VDISP_FRAME_WIDTH / 2
#define VDISP_HALF_HEIGHT 		VDISP_FRAME_HEIGHT / 2

SceBool sceCommonDialogIsRunning(void);

static uint8_t current_hook;
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t hook_refs[HOOKS_NUM];

SceDisplayFrameBuf bframe;

typedef enum HentaiStatus {
	HENTAI_IDLE,
	HENTAI_RUNNING,
	HENTAI_ERROR,
} HentaiStatus;

typedef struct ColorSurface {
	void *data;
	SceGxmColorSurface *surface;
	struct ColorSurface *next;
} ColorSurface;

typedef struct HentaiRenderTarget {
	const SceGxmRenderTarget *target;
	struct HentaiRenderTarget *next;
} HentaiRenderTarget;

// Set Decoder Stuff
JpegDecStatus status;
Jpeg_texture *texture;
static void *frameBufAddress[6];
int frameBufIndex = 0;

typedef enum RenderTargetInitStatus {
	RENDERTARGET_IDLE,
	RENDERTARGET_FIRST_SCENE,
	RENDERTARGET_ALL_FRAMEBUFFERS,
	RENDERTARGET_FINISHED,
	RENDERTARGET_ERROR,
} RenderTargetInitStatus;

static ColorSurface *cs;
static HentaiRenderTarget *hentaiRenderTarget;

// Hentai Thread Status
HentaiStatus hentaiStatus;

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

int bufferCount = 0;
SceBool firstRenderTarget;
SceBool mainRenderParams;
int intialSwaps = 0;


// Last SceGxmBeginScene Params
SceGxmContext *lastContext;
unsigned int lastFlags;
const SceGxmRenderTarget *lastRenderTarget;
const SceGxmValidRegion *lastValidRegion;
SceGxmSyncObject *lastVertexSyncObject;
SceGxmSyncObject *lastFragmentSyncObject;
const SceGxmColorSurface *lastColorSurface;
const SceGxmDepthStencilSurface *lastDepthStencil;
RenderTargetInitStatus renderTargetStatus;

// Last SceGxmEndScene Params
const SceGxmNotification *lastVertexNotification;
const SceGxmNotification *lastFragmentNotification;

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

void* alloc(size_t size) {
	SceUID memuid;
	void *addr;
	size = ALIGN(size, 4 * 1024);
	memuid = sceKernelAllocMemBlock("memory", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
	if (memuid < 0)
		return NULL;
	if (sceKernelGetMemBlockBase(memuid, &addr) < 0)
		return NULL;
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

void freeJpegTexture(void) {
	if (texture) {
		printf("Fucking with textures\n");
		if (texture->gxm_rtgt) {
			sceGxmDestroyRenderTarget(texture->gxm_rtgt);
		}
		if(texture->depth_UID) {
			graphicsFree(texture->depth_UID);
		}
		if(texture->palette_UID) {
			graphicsFree(texture->palette_UID);
		}
		graphicsFree(texture->data_UID);
		graphicsFree(texture->textureUID);
		texture = NULL;
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

	status = JPEG_DEC_IDLE;
	printf("Starting Shader Patcher Patch\n");

	int ret = TAI_CONTINUE(int, hook_refs[0], params, shaderPatcher);
	printf("Ran Shader Patcher\n");
	// Grabbing a reference to used shader patcher
	patcher = *shaderPatcher;
	printf("Aquired Shader Patcher: 0x%08x\n", ret);

	int res = sceGxmProgramCheck(textureVertexProgramGxp);
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

	if (vertices) {
		graphicsFree(verticesUid);
		printf("Freed Vertices\n");
	}
	if (indices) {
		graphicsFree(indicesUid);
		printf("Freed Indices\n");
	}
	if (texture) {
		freeJpegTexture();
		printf("Freed Texture\n");
	}

	vertices = gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SCE_GXM_MEMORY_ATTRIB_READ, 4 * sizeof(VertexV32T32), &verticesUid);

	indices = gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SCE_GXM_MEMORY_ATTRIB_READ, 4 * sizeof(unsigned short), &indicesUid);

	int i;
	for (i=0;i<4;i++){
		indices[i] = i;
	}

	matrix_init_orthographic(_vita2d_ortho_matrix, 0.0f, 960, 544, 0.0f, 0.0f, 1.0f);

	printf("Initialized Verteces and Indices\n");
	status = JPEG_DEC_WAITING;
	hentaiStatus = HENTAI_RUNNING;

	return ret;
}

int sceGxmEndScene_hentaiTime(SceGxmContext *context, const SceGxmNotification *vertexNotification, const SceGxmNotification *fragmentNotification)
{	
	int ret;

	if (status == JPEG_DEC_DISPLAY) {

		int width = texture->validWidth;
        int height = texture->validHeight;
		float x = width/2.0f;
		float y = height/2.0f;
		float zoom;

		if (width < VDISP_FRAME_WIDTH) {
			zoom = VDISP_FRAME_WIDTH / (float)width;
		} else if (height < VDISP_FRAME_HEIGHT) {
			zoom = VDISP_FRAME_HEIGHT / (float)height;
		} else {
			zoom = 1.0f;
		}

        minX = -(float)(zoom * x);
        minY = -(float)(zoom * y);
        maxX = minX + (float)(zoom * width);
        maxY = minY + (float)(zoom * height);

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

		float c = cosf(0);
		float s = sinf(0);
		int i;
		for (i = 0; i < 4; ++i) {
			float _x = vertices[i].x;
			float _y = vertices[i].y;
			vertices[i].x = _x*c - _y*s + (float)(VDISP_HALF_WIDTH);
			vertices[i].y = _x*s + _y*c + (float)(VDISP_HALF_HEIGHT);
		}

        printf("1: X:%f, Y:%f, Z:%f\n2: X:%f, Y:%f, Z:%f\n3: X:%f, Y:%f, Z:%f\n4: X:%f, Y:%f, Z:%f\n", vertices[0].x, vertices[0].y, vertices[0].z , vertices[1].x, vertices[1].y, vertices[1].z , vertices[2].x, vertices[2].y, vertices[2].z , vertices[3].x, vertices[3].y, vertices[3].z );

		/* Set texture */
		ret = sceGxmSetFragmentTexture(context, 0, &texture->gxm_tex);
        printf("Fragment Texture output: %d\n", ret);

        /* Set texture shaders */
        sceGxmSetVertexProgram(context, vertexProgram);
        sceGxmSetFragmentProgram(context, fragmentProgram);

        /* Draw the texture */

        void *vertex_wvp_buffer;

        ret = sceGxmReserveVertexDefaultUniformBuffer(context, &vertex_wvp_buffer);
        printf("Reserved Uniform Data output:0x%08x\n", ret);

        ret = sceGxmSetUniformDataF(vertex_wvp_buffer, wvp, 0, 16, _vita2d_ortho_matrix);
        printf("Uniform Data output: %d\n", ret);
		
        ret = sceGxmSetVertexStream(context, 0, vertices);
        printf("Vertex Stream output: %d\n", ret);

		printf("Starting to Draw\n");
        ret = sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, indices, 4);
        printf("GxmDraw Output: 0x%08x\n", ret);

        printf("Finished Drawing\n");
    }
	ret = TAI_CONTINUE(int, hook_refs[1], context, vertexNotification, fragmentNotification);
	return ret;
}

int sceGxmInitialize_hentaiTime(const SceGxmInitializeParams *params) {

	SceGxmInitializeParams initializeParams;

	printf("GXM Init!");
	printf("Old parameterBufferSize %d\n", params->parameterBufferSize);
	sceClibMemset(&initializeParams, 0, sizeof(SceGxmInitializeParams));

	initializeParams.flags = params->flags;
	initializeParams.displayQueueMaxPendingCount = params->displayQueueMaxPendingCount;
	initializeParams.displayQueueCallback = params->displayQueueCallback;
	initializeParams.displayQueueCallbackDataSize = params->displayQueueCallbackDataSize;
	initializeParams.parameterBufferSize = 6291456;
	printf("Display Queue Max Pending Count %d\n", initializeParams.displayQueueMaxPendingCount);
	printf("New parameterBufferSize %d\n", initializeParams.parameterBufferSize);

	int ret = TAI_CONTINUE(int, hook_refs[2], &initializeParams);
	printf("Gxm Initialize Out: 0x%08x\n", ret);
	
	return ret;
}

int sceDisplaySetFrameBuf_hentaiTime(const SceDisplayFrameBuf *pParam, SceDisplaySetBufSync sync) {
	if (renderTargetStatus == RENDERTARGET_FIRST_SCENE) {
		void *currentBackBuffer = pParam->base;
		if (frameBufIndex > 0) {
			int i = 0;
			while (i < frameBufIndex) {
				if (frameBufAddress[i] == currentBackBuffer) {
					frameBufAddress[frameBufIndex] = (int *)1;
					printf("Full Circle\n");
					renderTargetStatus = RENDERTARGET_ALL_FRAMEBUFFERS;
					goto skipBuffer;
				}
				i++;
			}
		}
		frameBufAddress[frameBufIndex] = currentBackBuffer;
		printf("FrameBufAddress Pointer Address: %x\nOriginal FrameBufAddress: %x\nFrameBuffer Size: %dx%d\n\n", frameBufAddress[frameBufIndex], currentBackBuffer, pParam->width, pParam->height);
		frameBufIndex++;
	}
	skipBuffer:
	return TAI_CONTINUE(int, hook_refs[3], pParam, sync);;

}

int sceGxmCreateRenderTarget_hentaiTime(const SceGxmRenderTargetParams *params, SceGxmRenderTarget **renderTarget) {
	int ret = TAI_CONTINUE(int, hook_refs[4], params, renderTarget);
	printf("Render Target Params\nResolution: %dx%d\nScenes Per Frame: %d\n\n", params->width, params->height, params->scenesPerFrame);
	return ret;
}

int sceGxmBeginScene_hentaiTime(SceGxmContext *context, unsigned int flags, const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion,
									SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject, const SceGxmColorSurface *colorSurface, const SceGxmDepthStencilSurface *depthStencil ) {
	ColorSurface *current = cs;
	HentaiRenderTarget *currentTarget = hentaiRenderTarget;
	SceBool disable = SCE_TRUE;
	if (renderTargetStatus == RENDERTARGET_IDLE) {
		renderTargetStatus = RENDERTARGET_FIRST_SCENE;
	}
	if (depthStencil) {
		printf("Depth: %f", depthStencil->backgroundDepth);
	}
	if (status == JPEG_DEC_DECODED && renderTargetStatus == RENDERTARGET_FINISHED) {
		while (currentTarget->next) {
			if (currentTarget->target == renderTarget) {
				printf("Enable Draw on Scene\n");
				status = JPEG_DEC_DISPLAY;
				break;
			}
			currentTarget = currentTarget->next;
		}
	} else if (status == JPEG_DEC_DISPLAY && renderTargetStatus == RENDERTARGET_FINISHED) {
		while (currentTarget->next) {
			if (currentTarget->target != renderTarget) {
				currentTarget = currentTarget->next;
			} else {
				disable = SCE_FALSE;
				break;
			}
		}
		if (disable == SCE_TRUE) {
			printf("Disable Draw on Scene\n");
			status = JPEG_DEC_DECODED;
		}
	}
	if (renderTargetStatus == RENDERTARGET_ALL_FRAMEBUFFERS) {
		while (current->next) {
			printf("First Loop: Current Data: %x Current Surface: %x\n", current->data, current->surface);
			if (current->surface == colorSurface) {
				for (int i=0;i<frameBufIndex;i++) {
					printf("Second Loop\n");
					if (frameBufAddress[i] == current->data) {
						while (currentTarget->next)
						{
							printf("Third Loop\n");
							if (currentTarget->target == renderTarget) {
								printf("Render Target Full Circle\n");
								renderTargetStatus = RENDERTARGET_FINISHED;
								goto renderTargetFinish;
							}
							currentTarget = currentTarget->next;
						}
						currentTarget->target = renderTarget;
						currentTarget->next = (HentaiRenderTarget *)alloc(sizeof(HentaiRenderTarget));
						printf("\nAdded Hentai Render Target: %x\n\n", currentTarget->target);
						goto renderTargetFinish;
					}
				}
			}
			current = current->next;
		}
	}
	renderTargetFinish:
	printf("Ran GxmBeginScene:\nGXM Flags: %d Render Target: %x\n Color Surface: %x\n", flags, renderTarget, colorSurface);
	if (validRegion) {
		printf("Valid Region: %dx%d to %dx%d\n",validRegion->xMin, validRegion->yMin, validRegion->xMax, validRegion->yMax);
	}

	return TAI_CONTINUE(int, hook_refs[5], context, flags, renderTarget, validRegion, vertexSyncObject, fragmentSyncObject, colorSurface, depthStencil);
}

int sceGxmColorSurfaceInit_hentaiTime(SceGxmColorSurface *surface, SceGxmColorFormat colorFormat, SceGxmColorSurfaceType surfaceType, SceGxmColorSurfaceScaleMode scaleMode,
										SceGxmOutputRegisterSize outputRegisterSize, unsigned int width, unsigned int height, unsigned int strideInPixels, void *data) {
	ColorSurface *current = cs;
	while (current->next)
	{
		current = current->next;
	}
	current->data = data;
	current->surface = surface;
	//ColorSurface *nextSurface;
	//sceClibMemset(&nextSurface, 0, sizeof(ColorSurface));
	current->next = (ColorSurface *)alloc(sizeof(ColorSurface));

	printf("Ran sceGxmColorSurfaceInit: Color Surface: %x\nCurrent Data: %x\nData: %x\n", surface, current->data, data);
	
	return TAI_CONTINUE(int, hook_refs[6], surface, colorFormat, surfaceType, scaleMode, outputRegisterSize, width, height, strideInPixels, data);;
}

int hentai_thread(SceSize args, void *argp) {
	int ret;
	// Random Number Generator Variables
	int interval;
	SceUInt randomNumber;
	int max = 15;
	int min = 0;
	// Control Variable
	SceCtrlData ctrl;
	// Framebuffer Stuff
	bframe.size = sizeof(SceDisplayFrameBuf);
	// Main Loop
	while(1) {
		sceKernelDelayThread(1000);
		while (hentaiStatus == HENTAI_RUNNING && renderTargetStatus == RENDERTARGET_FINISHED) {
			sceKernelDelayThread(1000);
			if (status == JPEG_DEC_WAITING) {
				wait:
				sceKernelGetRandomNumber(&randomNumber, 4);
				interval = ((randomNumber % (max-min+1)) + min)*1000000;
				printf("I Will Show A Picture in %d Seconds\n", interval/1000000);
				sceKernelDelayThread(interval);
				SceBool running = sceCommonDialogIsRunning();
				if (running) {
					printf("Common Dialog is Running: %d\n", running);
					goto wait;
				}
				printf("Common Dialog Isn't Running: %d\nContinuing...\n", running);
				// Only enable for Debug
				//goto wait;
			}
			if (status != JPEG_DEC_DECODED && status != JPEG_DEC_DECODING && status != JPEG_DEC_ERROR && status != JPEG_DEC_DISPLAY) {
				status = JPEG_DEC_DECODING;
			}
			if (status == JPEG_DEC_DECODING) {
				sceDisplayGetFrameBuf(&bframe, 0);
				printf("Framebuffer Resolution: %dx%d\n", bframe.width, bframe.height);
				if (texture) {
					freeJpegTexture();
					printf("Freed Texture\n");
				}
				ret = rh_JPEG_decoder_initialize();
				if (ret < 0) {
					status = JPEG_DEC_ERROR;
					continue;
				}
				printf("Decoding Texture\n");
				texture = rh_load_JPEG_file(FILENAME, &bframe);
				if (texture == NULL) {
					printf("Error Decoding Texture\n");
					rh_JPEG_decoder_finish();
					status = JPEG_DEC_ERROR;
					continue;
				}
				printf("Decoded Texture\n");
				rh_JPEG_decoder_finish();
				status = JPEG_DEC_DECODED;
			}
			if (status == JPEG_DEC_DECODED || status == JPEG_DEC_DISPLAY) {
				sceCtrlPeekBufferPositive(0, &ctrl, 1);
				if (ctrl.buttons == (SCE_CTRL_LTRIGGER | SCE_CTRL_CIRCLE)) {
					status = JPEG_DEC_WAITING;
				}
			}
			if (status == JPEG_DEC_ERROR) {
				printf("Error Occured with JPEG Decoder. We're just gonna try again later\n");
				status = JPEG_DEC_WAITING;
			}
		}
		if (hentaiStatus == HENTAI_ERROR) {
			printf("Error Occurred. Stopping Plugin\n");
			module_stop(NULL, NULL);
			sceKernelExitDeleteThread(0);
		}
	}
	return 0;
}


void _start() __attribute__ ((weak, alias ("module_start")));

int module_start(SceSize argc, const void *args) {

	hookFunction(0x05032658, sceGxmShaderPatcherCreate_hentaiTime);
	printf("Initialized Hook 0\n");
	hookFunction(0xFE300E2F, sceGxmEndScene_hentaiTime);
	printf("Initialized Hook 1\n");			
	hookFunction(0xB0F1E4EC, sceGxmInitialize_hentaiTime);
	printf("Initialized Hook 2\n");
	hookFunction(0x7A410B64, sceDisplaySetFrameBuf_hentaiTime);
	printf("Initialized Hook 3\n");
	hookFunction(0x207AF96B, sceGxmCreateRenderTarget_hentaiTime);
	printf("Initialized Hook 4\n");
	hookFunction(0x8734FF4E, sceGxmBeginScene_hentaiTime);
	printf("Initialized Hook 5\n");
	hookFunction(0xED0F6E25, sceGxmColorSurfaceInit_hentaiTime);
	printf("Initialized Hook 6\n");

	SceUID thid;
	thid = sceKernelCreateThread("hentai_thread", hentai_thread, 0x60, 0x10000, 0, 0, NULL);

	if (thid >= 0)
		sceKernelStartThread(thid, 0, NULL); 
	
	//sceClibMemset(&cs, 0, sizeof(ColorSurface));
	cs = (ColorSurface *)alloc(sizeof(ColorSurface));
	hentaiRenderTarget = (HentaiRenderTarget *)alloc(sizeof(HentaiRenderTarget));

	return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize argc, const void *args) {

	printf("Stopping");
	status = JPEG_DEC_NO_INIT;
	rh_JPEG_decoder_finish();
	if (vertices) {
		graphicsFree(verticesUid);
		printf("Freed Vertices");
	}
	if (indices) {
		graphicsFree(indicesUid);
		printf("Freed Indices");
	}

	// Freeing hooks
	while (current_hook-- > 0){
		taiHookRelease(hooks[current_hook], hook_refs[current_hook]);
		printf("Released Hook %d", current_hook);
	}
	
	return SCE_KERNEL_STOP_SUCCESS;	
}