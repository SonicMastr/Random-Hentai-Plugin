#include "hentai.h"

#include "common.h"
#include "util.h"
#include "dir.h"

#define FILENAME "ux0:/data/randomhentai/saved/247566.jpg"

#define MAX_FRAMEBUFADD 10
#define MAX_RENDER_TARGETS 64
#define MAX_HENTAI_TARGETS 10
static void *frameBufAddress[MAX_FRAMEBUFADD];
static void *renderTargetData[MAX_RENDER_TARGETS];
static SceGxmColorSurface *renderTargetSurface[MAX_RENDER_TARGETS];
static SceGxmRenderTarget *hentaiRenderTarget[MAX_HENTAI_TARGETS];

typedef enum HentaiTargetStatus {
	HENTAITARGET_NONE,
	HENTAITARGET_ALL_FRAMEBUFFERS,
	HENTAITARGET_FINISHED,
	HENTAITARGET_ERROR,
} HentaiTargetStatus;

/* Shaders */
extern const SceGxmProgram _binary_texture_v_gxp_start;
extern const SceGxmProgram _binary_texture_f_gxp_start;
static const SceGxmProgram *const textureVertexProgramGxp	= &_binary_texture_v_gxp_start;
static const SceGxmProgram *const textureFragmentProgramGxp	= &_binary_texture_f_gxp_start;
static SceGxmShaderPatcherId	vertexProgramId;
static SceGxmShaderPatcherId	fragmentProgramId;
static SceGxmVertexProgram		*vertexProgram = NULL;
static SceGxmFragmentProgram	*fragmentProgram = NULL;
static const SceGxmProgramParameter *paramPositionAttribute = NULL;
static const SceGxmProgramParameter *paramTextureAttribute = NULL;
/* Vertices and Indices */
static SceUID					verticesUid;
static SceUID					indicesUid;
static VertexV32T32	*vertices = NULL;
static SceUInt16	*indices = NULL;
/* Texture Data */
Jpeg_texture *texture;
/* Statuses */
JpegDecStatus jpegStatus;
HentaiTargetStatus hentaiStatus;

void hentaiInit(void) {
	sceClibMemset(frameBufAddress, 0xFF, sizeof(frameBufAddress));
	sceClibMemset(renderTargetData, 0xFF, sizeof(renderTargetData));
	sceClibMemset(renderTargetSurface, 0xFF, sizeof(renderTargetSurface));
	jpegStatus = JPEG_DEC_WAITING;
}

void hentaiCleanup(void) {
	jpegStatus = JPEG_DEC_NO_INIT;
	rh_JPEG_decoder_finish();
	if (vertices)
		graphicsFree(verticesUid);
	if (indices)
		graphicsFree(indicesUid);
	if (texture)
		freeJpegTexture(texture);
}

SceBool hentaiHasAllFramebuffers(void) {
	return (hentaiStatus == HENTAITARGET_ALL_FRAMEBUFFERS || hentaiStatus == HENTAITARGET_FINISHED) ? SCE_TRUE : SCE_FALSE;
}

SceBool hentaiHentaiTargetIsFinished(void) {
	return (hentaiStatus == HENTAITARGET_FINISHED) ? SCE_TRUE : SCE_FALSE;
}

JpegDecStatus hentaiGetJpegStatus(void) {
	return jpegStatus;
}

void hentaiGxmInit(SceGxmInitializeParams *newInitializationParams, const SceGxmInitializeParams *params) {
	sceClibMemset(newInitializationParams, 0, sizeof(SceGxmInitializeParams));
	newInitializationParams->flags = params->flags;
	newInitializationParams->displayQueueMaxPendingCount = params->displayQueueMaxPendingCount;
	newInitializationParams->displayQueueCallback = params->displayQueueCallback;
	newInitializationParams->displayQueueCallbackDataSize = params->displayQueueCallbackDataSize;
	newInitializationParams->parameterBufferSize = 0x600000;
}

int hentaiShaderPatcher(SceGxmShaderPatcher **shaderPatcher) {
	SceGxmShaderPatcher *patcher = *shaderPatcher;
	GLZ(sceGxmProgramCheck(textureVertexProgramGxp));
	GLZ(sceGxmProgramCheck(textureFragmentProgramGxp));
	GLZ(sceGxmShaderPatcherRegisterProgram(patcher, textureVertexProgramGxp, &vertexProgramId));
	GLZ(sceGxmShaderPatcherRegisterProgram(patcher, textureFragmentProgramGxp, &fragmentProgramId));
	paramPositionAttribute = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "aPosition");
	paramTextureAttribute = sceGxmProgramFindParameterByName(textureVertexProgramGxp, "aTexcoord");
	if (paramTextureAttribute == NULL || paramPositionAttribute == NULL)
		return -1;

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

	GLZ(sceGxmShaderPatcherCreateVertexProgram(
		patcher,
		vertexProgramId,
		vertexAttributes, 2,
		vertexStreams, 1,
		&vertexProgram));

	SceGxmBlendInfo blendInfo;

	blendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
	blendInfo.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
	blendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
	blendInfo.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
	blendInfo.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendInfo.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
	blendInfo.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;

	GLZ(sceGxmShaderPatcherCreateFragmentProgram(
		patcher,
		fragmentProgramId,
		SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
		SCE_GXM_MULTISAMPLE_4X,
		&blendInfo,
		textureVertexProgramGxp,
		&fragmentProgram));
	/* Sometimes this may be ran more than once. Need to account for that */
	if (vertices)
		graphicsFree(verticesUid);
	if (indices)
		graphicsFree(indicesUid);
	if (texture)
		freeJpegTexture(texture);
	vertices = gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SCE_GXM_MEMORY_ATTRIB_READ, 4 * sizeof(VertexV32T32), &verticesUid);
	indices = gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SCE_GXM_MEMORY_ATTRIB_READ, 4 * sizeof(unsigned short), &indicesUid);
	int i;
	for (i=0;i<4;i++){
		indices[i] = i;
	}

	return 0;
}

void hentaiNextFrameBufAddress(const SceDisplayFrameBuf *pParam, SceBool *next) {
	for (int i = 0; i < MAX_FRAMEBUFADD; i++) {
		if (frameBufAddress[i] == pParam->base) {
			*next = SCE_FALSE;
			hentaiStatus = HENTAITARGET_ALL_FRAMEBUFFERS;
			break; /* Full Circle */
		} else if ((int)frameBufAddress[i] == -1) {
			frameBufAddress[i] = pParam->base;
			break;
		}	
	}
}

void hentaiAddColorSurface(SceGxmColorSurface *surface, void *data) {
	static int i;
	renderTargetSurface[i] = surface;
	renderTargetData[i] = data;
	i++;
}

void hentaiCompareTargetAddress(const SceGxmRenderTarget *renderTarget, const SceGxmColorSurface *colorSurface) {
	static SceGxmRenderTarget *lastRenderTarget;
	static int fail;
	if (fail > 20)
		hentaiStatus = HENTAITARGET_ERROR;
	if (lastRenderTarget == renderTarget) {
		hentaiRenderTarget[0] = renderTarget;
		hentaiStatus = HENTAITARGET_FINISHED;
		return;
	}
	lastRenderTarget = renderTarget;
	for (int i = 0; i < MAX_RENDER_TARGETS; i++) {
		if (renderTargetSurface[i] == colorSurface) {
			for (int j = 0; j < MAX_FRAMEBUFADD; j++) {
				if (frameBufAddress[j] == renderTargetData[i]) {
					for (int k = 0; k < MAX_HENTAI_TARGETS; k++) {
						if (hentaiRenderTarget[k] == renderTarget) {
							hentaiStatus = HENTAITARGET_FINISHED;
							break; // Full Circle
						} else if ((int)hentaiRenderTarget[k] == -1) {
							hentaiRenderTarget[k] = renderTarget;
							break; // Saved the Render Target
						}
					}
				}
			}
			break;
		} else if ((int)renderTargetSurface[i] == -1) {
			fail++;
			break; // End of Array
		}	
	}
}

void hentaiSetDisplayState(const SceGxmRenderTarget *renderTarget) {
	if (jpegStatus == JPEG_DEC_DISPLAY || jpegStatus == JPEG_DEC_DECODED) {
		for (int i = 0; i < MAX_HENTAI_TARGETS; i++) {
			if (hentaiRenderTarget[i] == renderTarget) {
				jpegStatus = JPEG_DEC_DISPLAY; //RenderTarget Matches
				break; 
			} else if ((int)hentaiRenderTarget[i] == -1) {
				jpegStatus = JPEG_DEC_DECODED;
				break; // End of Array
			}
		}
	}
}

void hentaiDraw(SceGxmContext *context) {
	int count = 0;
	if (jpegStatus == JPEG_DEC_DISPLAY) {
		int width = texture->validWidth;
        int height = texture->validHeight;
		float scaleX, scaleY, minX, minY, maxX, maxY, totalsca;

		scaleX = width/(float)960;
		scaleY = height/(float)544;

		float totalScale = 1/scaleX;

		scaleX = scaleX*totalScale;
		scaleY = scaleY*totalScale;

		minX = -scaleX;
		minY = -scaleY;
		maxX = minX + (scaleX * 2);
		maxY = minY + (scaleY * 2);

		vertices[0].x = minX;
		vertices[0].y = minY;
		vertices[0].z = 0.0f;
		vertices[0].u = 0.0f;
		vertices[0].v = 1.0f;

		vertices[1].x = maxX;
		vertices[1].y = minY;
		vertices[1].z = 0.0f;
		vertices[1].u = 1.0f;
		vertices[1].v = 1.0f;

		vertices[2].x = minX;
		vertices[2].y = maxY;
		vertices[2].z = 0.0f;
		vertices[2].u = 0.0f;
		vertices[2].v = 0.0f;

		vertices[3].x = maxX;
		vertices[3].y = maxY;
		vertices[3].z = 0.0f;
		vertices[3].u = 1.0f;
		vertices[3].v = 0.0f;

		//printf("1: X:%f, Y:%f, Z:%f\n2: X:%f, Y:%f, Z:%f\n3: X:%f, Y:%f, Z:%f\n4: X:%f, Y:%f, Z:%f\n", vertices[0].x, vertices[0].y, vertices[0].z , vertices[1].x, vertices[1].y, vertices[1].z , vertices[2].x, vertices[2].y, vertices[2].z , vertices[3].x, vertices[3].y, vertices[3].z );

		/* Set texture */
		sceGxmSetFragmentTexture(context, 0, &texture->gxm_tex);
		/* Set texture shaders */
		sceGxmSetVertexProgram(context, vertexProgram);
		sceGxmSetFragmentProgram(context, fragmentProgram);
		/* Depth/Stencil Tests */
		sceGxmSetFrontDepthFunc(
			context,
			SCE_GXM_DEPTH_FUNC_ALWAYS
		);
		sceGxmSetFrontStencilFunc(
			context,
			SCE_GXM_STENCIL_FUNC_ALWAYS,
			SCE_GXM_STENCIL_OP_KEEP,
			SCE_GXM_STENCIL_OP_KEEP,
			SCE_GXM_STENCIL_OP_KEEP,
			0xFF,
			0xFF);
		/* Draw the texture */	
		sceGxmSetVertexStream(context, 0, vertices);
		sceGxmDraw(context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, indices, 4);
	}
}

int hentaiRandomHentai(void) {
	char *filename = getRandomImage();
	if (!filename) {
		jpegStatus = JPEG_DEC_ERROR;
		return -1;
	}
	char fullPath[256] = "ux0:data/randomhentai/saved/\0";
	strcat(fullPath, filename);
	printf("Filename: %s\n", fullPath);
	if (texture)
		freeJpegTexture(texture);
	if (rh_JPEG_decoder_initialize() < 0) {
		jpegStatus = JPEG_DEC_ERROR;
		return -1;
	}
	texture = rh_load_JPEG_file(fullPath);
	if (!texture) {
		rh_JPEG_decoder_finish();
		jpegStatus = JPEG_DEC_ERROR;
		return -1;
	}
	rh_JPEG_decoder_finish();
	jpegStatus = JPEG_DEC_DECODED;
	return 0;
}

int hentaiReset(void) {
	jpegStatus = JPEG_DEC_WAITING;
	return 0;
}