#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
// For FlashWindowEx, FLASHWINFO, FLASHW_TRAY
#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#endif

#include <GL/glew.h>
#include <engine/storage.h>

#include "SDL.h"
#include "SDL_opengl.h"
#include "SDL_syswm.h"
#include <base/detect.h>
#include <base/math.h>
#include <cmath>
#include <stdlib.h>

#include "SDL_hints.h"

#if defined(SDL_VIDEO_DRIVER_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <engine/shared/config.h>

#include <base/tl/threading.h>

#if defined(CONF_VIDEORECORDER)
#include "video.h"
#endif

#include "backend_sdl.h"
#include "graphics_threaded.h"

#include "opengl_sl.h"
#include "opengl_sl_program.h"

#include <engine/shared/image_manipulation.h>

#ifdef __MINGW32__
extern "C" {
int putenv(const char *);
}
#endif

/*
	sync_barrier - creates a full hardware fence
*/
#if defined(__GNUC__)
inline void sync_barrier()
{
	__sync_synchronize();
}
#elif defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
inline void sync_barrier()
{
	MemoryBarrier();
}
#else
#error missing atomic implementation for this compiler
#endif

// ------------ CGraphicsBackend_Threaded

void CGraphicsBackend_Threaded::ThreadFunc(void *pUser)
{
	CGraphicsBackend_Threaded *pThis = (CGraphicsBackend_Threaded *)pUser;

	while(!pThis->m_Shutdown)
	{
		pThis->m_Activity.Wait();
		if(pThis->m_pBuffer)
		{
#ifdef CONF_PLATFORM_MACOS
			CAutoreleasePool AutoreleasePool;
#endif
			pThis->m_pProcessor->RunBuffer(pThis->m_pBuffer);

			sync_barrier();
			pThis->m_pBuffer = 0x0;
			pThis->m_BufferDone.Signal();
		}
#if defined(CONF_VIDEORECORDER)
		if(IVideo::Current())
			IVideo::Current()->NextVideoFrameThread();
#endif
	}
}

CGraphicsBackend_Threaded::CGraphicsBackend_Threaded()
{
	m_pBuffer = 0x0;
	m_pProcessor = 0x0;
	m_pThread = 0x0;
}

void CGraphicsBackend_Threaded::StartProcessor(ICommandProcessor *pProcessor)
{
	m_Shutdown = false;
	m_pProcessor = pProcessor;
	m_pThread = thread_init(ThreadFunc, this, "CGraphicsBackend_Threaded");
	m_BufferDone.Signal();
}

void CGraphicsBackend_Threaded::StopProcessor()
{
	m_Shutdown = true;
	m_Activity.Signal();
	if(m_pThread)
		thread_wait(m_pThread);
}

void CGraphicsBackend_Threaded::RunBuffer(CCommandBuffer *pBuffer)
{
	WaitForIdle();
	m_pBuffer = pBuffer;
	m_Activity.Signal();
}

bool CGraphicsBackend_Threaded::IsIdle() const
{
	return m_pBuffer == 0x0;
}

void CGraphicsBackend_Threaded::WaitForIdle()
{
	while(m_pBuffer != 0x0)
		m_BufferDone.Wait();
}

static bool Texture2DTo3D(void *pImageBuffer, int ImageWidth, int ImageHeight, int ImageColorChannelCount, int SplitCountWidth, int SplitCountHeight, void *pTarget3DImageData, int &Target3DImageWidth, int &Target3DImageHeight)
{
	Target3DImageWidth = ImageWidth / SplitCountWidth;
	Target3DImageHeight = ImageHeight / SplitCountHeight;

	size_t FullImageWidth = (size_t)ImageWidth * ImageColorChannelCount;

	for(int Y = 0; Y < SplitCountHeight; ++Y)
	{
		for(int X = 0; X < SplitCountWidth; ++X)
		{
			for(int Y3D = 0; Y3D < Target3DImageHeight; ++Y3D)
			{
				int DepthIndex = X + Y * SplitCountWidth;

				size_t TargetImageFullWidth = (size_t)Target3DImageWidth * ImageColorChannelCount;
				size_t TargetImageFullSize = (size_t)TargetImageFullWidth * Target3DImageHeight;
				ptrdiff_t ImageOffset = (ptrdiff_t)(((size_t)Y * FullImageWidth * (size_t)Target3DImageHeight) + ((size_t)Y3D * FullImageWidth) + ((size_t)X * TargetImageFullWidth));
				ptrdiff_t TargetImageOffset = (ptrdiff_t)(TargetImageFullSize * (size_t)DepthIndex + ((size_t)Y3D * TargetImageFullWidth));
				mem_copy(((uint8_t *)pTarget3DImageData) + TargetImageOffset, ((uint8_t *)pImageBuffer) + (ptrdiff_t)(ImageOffset), TargetImageFullWidth);
			}
		}
	}

	return true;
}

// ------------ CCommandProcessorFragment_General

void CCommandProcessorFragment_General::Cmd_Signal(const CCommandBuffer::SCommand_Signal *pCommand)
{
	pCommand->m_pSemaphore->Signal();
}

bool CCommandProcessorFragment_General::RunCommand(const CCommandBuffer::SCommand *pBaseCommand)
{
	switch(pBaseCommand->m_Cmd)
	{
	case CCommandBuffer::CMD_NOP: break;
	case CCommandBuffer::CMD_SIGNAL: Cmd_Signal(static_cast<const CCommandBuffer::SCommand_Signal *>(pBaseCommand)); break;
	default: return false;
	}

	return true;
}

// ------------ CCommandProcessorFragment_SDL

static void ParseVersionString(const char *pStr, int &VersionMajor, int &VersionMinor, int &VersionPatch)
{
	if(pStr)
	{
		char aCurNumberStr[32];
		size_t CurNumberStrLen = 0;
		size_t TotalNumbersPassed = 0;
		int aNumbers[3] = {0};
		bool LastWasNumber = false;
		while(*pStr && TotalNumbersPassed < 3)
		{
			if(*pStr >= '0' && *pStr <= '9')
			{
				aCurNumberStr[CurNumberStrLen++] = (char)*pStr;
				LastWasNumber = true;
			}
			else if(LastWasNumber && (*pStr == '.' || *pStr == ' ' || *pStr == '\0'))
			{
				int CurNumber = 0;
				if(CurNumberStrLen > 0)
				{
					aCurNumberStr[CurNumberStrLen] = 0;
					CurNumber = str_toint(aCurNumberStr);
					aNumbers[TotalNumbersPassed++] = CurNumber;
					CurNumberStrLen = 0;
				}

				LastWasNumber = false;

				if(*pStr != '.')
					break;
			}
			else
			{
				break;
			}

			++pStr;
		}

		VersionMajor = aNumbers[0];
		VersionMinor = aNumbers[1];
		VersionPatch = aNumbers[2];
	}
}

static const char *GetGLErrorName(GLenum Type)
{
	if(Type == GL_DEBUG_TYPE_ERROR)
		return "ERROR";
	else if(Type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR)
		return "DEPRECATED BEHAVIOR";
	else if(Type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR)
		return "UNDEFINED BEHAVIOR";
	else if(Type == GL_DEBUG_TYPE_PORTABILITY)
		return "PORTABILITY";
	else if(Type == GL_DEBUG_TYPE_PERFORMANCE)
		return "PERFORMANCE";
	else if(Type == GL_DEBUG_TYPE_OTHER)
		return "OTHER";
	else if(Type == GL_DEBUG_TYPE_MARKER)
		return "MARKER";
	else if(Type == GL_DEBUG_TYPE_PUSH_GROUP)
		return "PUSH_GROUP";
	else if(Type == GL_DEBUG_TYPE_POP_GROUP)
		return "POP_GROUP";
	return "UNKNOWN";
};

static const char *GetGLSeverity(GLenum Type)
{
	if(Type == GL_DEBUG_SEVERITY_HIGH)
		return "high"; // All OpenGL Errors, shader compilation/linking errors, or highly-dangerous undefined behavior
	else if(Type == GL_DEBUG_SEVERITY_MEDIUM)
		return "medium"; // Major performance warnings, shader compilation/linking warnings, or the use of deprecated functionality
	else if(Type == GL_DEBUG_SEVERITY_LOW)
		return "low"; // Redundant state change performance warning, or unimportant undefined behavior
	else if(Type == GL_DEBUG_SEVERITY_NOTIFICATION)
		return "notification"; // Anything that isn't an error or performance issue.

	return "unknown";
}

static void GLAPIENTRY
GfxOpenGLMessageCallback(GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar *message,
	const void *userParam)
{
	dbg_msg("gfx", "[%s] (importance: %s) %s", GetGLErrorName(type), GetGLSeverity(severity), message);
}

void CCommandProcessorFragment_SDL::Cmd_Init(const SCommand_Init *pCommand)
{
	m_GLContext = pCommand->m_GLContext;
	m_pWindow = pCommand->m_pWindow;
	SDL_GL_MakeCurrent(m_pWindow, m_GLContext);

	// set some default settings
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	glAlphaFunc(GL_GREATER, 0);
	glEnable(GL_ALPHA_TEST);
	glDepthMask(0);

	if(g_Config.m_DbgGfx)
	{
		if(GLEW_KHR_debug || GLEW_ARB_debug_output)
		{
			// During init, enable debug output
			if(GLEW_KHR_debug)
			{
				glEnable(GL_DEBUG_OUTPUT);
				glDebugMessageCallback(GfxOpenGLMessageCallback, 0);
			}
			else if(GLEW_ARB_debug_output)
			{
				glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
				glDebugMessageCallbackARB(GfxOpenGLMessageCallback, 0);
			}
			dbg_msg("gfx", "Enabled OpenGL debug mode");
		}
		else
			dbg_msg("gfx", "Requested OpenGL debug mode, but the driver does not support the required extension");
	}

	const char *pVendorString = (const char *)glGetString(GL_VENDOR);
	dbg_msg("opengl", "Vendor string: %s", pVendorString);

	// check what this context can do
	const char *pVersionString = (const char *)glGetString(GL_VERSION);
	dbg_msg("opengl", "Version string: %s", pVersionString);

	const char *pRendererString = (const char *)glGetString(GL_RENDERER);

	str_copy(pCommand->m_pVendorString, pVendorString, gs_GPUInfoStringSize);
	str_copy(pCommand->m_pVersionString, pVersionString, gs_GPUInfoStringSize);
	str_copy(pCommand->m_pRendererString, pRendererString, gs_GPUInfoStringSize);

	// parse version string
	ParseVersionString(pVersionString, pCommand->m_pCapabilities->m_ContextMajor, pCommand->m_pCapabilities->m_ContextMinor, pCommand->m_pCapabilities->m_ContextPatch);

	*pCommand->m_pInitError = 0;

	int BlocklistMajor = -1, BlocklistMinor = -1, BlocklistPatch = -1;
	const char *pErrString = ParseBlocklistDriverVersions(pVendorString, pVersionString, BlocklistMajor, BlocklistMinor, BlocklistPatch);
	//if the driver is buggy, and the requested GL version is the default, fallback
	if(pErrString != NULL && pCommand->m_RequestedMajor == 3 && pCommand->m_RequestedMinor == 0 && pCommand->m_RequestedPatch == 0)
	{
		// if not already in the error state, set the GL version
		if(g_Config.m_GfxDriverIsBlocked == 0)
		{
			// fallback to known good GL version
			pCommand->m_pCapabilities->m_ContextMajor = BlocklistMajor;
			pCommand->m_pCapabilities->m_ContextMinor = BlocklistMinor;
			pCommand->m_pCapabilities->m_ContextPatch = BlocklistPatch;

			// set backend error string
			*pCommand->m_pErrStringPtr = pErrString;
			*pCommand->m_pInitError = -2;

			g_Config.m_GfxDriverIsBlocked = 1;
		}
	}
	// if the driver was in a blocked error state, but is not anymore, reset all config variables
	else if(pErrString == NULL && g_Config.m_GfxDriverIsBlocked == 1)
	{
		pCommand->m_pCapabilities->m_ContextMajor = 3;
		pCommand->m_pCapabilities->m_ContextMinor = 0;
		pCommand->m_pCapabilities->m_ContextPatch = 0;

		// tell the caller to reinitialize the context
		*pCommand->m_pInitError = -2;

		g_Config.m_GfxDriverIsBlocked = 0;
	}

	int MajorV = pCommand->m_pCapabilities->m_ContextMajor;
	int MinorV = pCommand->m_pCapabilities->m_ContextMinor;

	if(*pCommand->m_pInitError == 0)
	{
		if(MajorV < pCommand->m_RequestedMajor)
		{
			*pCommand->m_pInitError = -2;
		}
		else if(MajorV == pCommand->m_RequestedMajor)
		{
			if(MinorV < pCommand->m_RequestedMinor)
			{
				*pCommand->m_pInitError = -2;
			}
			else if(MinorV == pCommand->m_RequestedMinor)
			{
				int PatchV = pCommand->m_pCapabilities->m_ContextPatch;
				if(PatchV < pCommand->m_RequestedPatch)
				{
					*pCommand->m_pInitError = -2;
				}
			}
		}
	}

	if(*pCommand->m_pInitError == 0)
	{
		MajorV = pCommand->m_RequestedMajor;
		MinorV = pCommand->m_RequestedMinor;

		pCommand->m_pCapabilities->m_2DArrayTexturesAsExtension = false;
		pCommand->m_pCapabilities->m_NPOTTextures = true;

		if(MajorV >= 4 || (MajorV == 3 && MinorV == 3))
		{
			pCommand->m_pCapabilities->m_TileBuffering = true;
			pCommand->m_pCapabilities->m_QuadBuffering = true;
			pCommand->m_pCapabilities->m_TextBuffering = true;
			pCommand->m_pCapabilities->m_QuadContainerBuffering = true;
			pCommand->m_pCapabilities->m_ShaderSupport = true;

			pCommand->m_pCapabilities->m_MipMapping = true;
			pCommand->m_pCapabilities->m_3DTextures = true;
			pCommand->m_pCapabilities->m_2DArrayTextures = true;
		}
		else if(MajorV == 3)
		{
			pCommand->m_pCapabilities->m_MipMapping = true;
			// check for context native 2D array texture size
			pCommand->m_pCapabilities->m_3DTextures = false;
			pCommand->m_pCapabilities->m_2DArrayTextures = false;
			pCommand->m_pCapabilities->m_ShaderSupport = true;

			int TextureLayers = 0;
			glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &TextureLayers);
			if(TextureLayers >= 256)
			{
				pCommand->m_pCapabilities->m_2DArrayTextures = true;
			}

			int Texture3DSize = 0;
			glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &Texture3DSize);
			if(Texture3DSize >= 256)
			{
				pCommand->m_pCapabilities->m_3DTextures = true;
			}

			if(!pCommand->m_pCapabilities->m_3DTextures && !pCommand->m_pCapabilities->m_2DArrayTextures)
			{
				*pCommand->m_pInitError = -2;
				pCommand->m_pCapabilities->m_ContextMajor = 1;
				pCommand->m_pCapabilities->m_ContextMinor = 5;
				pCommand->m_pCapabilities->m_ContextPatch = 0;
			}

			pCommand->m_pCapabilities->m_TileBuffering = pCommand->m_pCapabilities->m_2DArrayTextures || pCommand->m_pCapabilities->m_3DTextures;
			pCommand->m_pCapabilities->m_QuadBuffering = false;
			pCommand->m_pCapabilities->m_TextBuffering = false;
			pCommand->m_pCapabilities->m_QuadContainerBuffering = false;
		}
		else if(MajorV == 2)
		{
			pCommand->m_pCapabilities->m_MipMapping = true;
			// check for context extension: 2D array texture and its max size
			pCommand->m_pCapabilities->m_3DTextures = false;
			pCommand->m_pCapabilities->m_2DArrayTextures = false;

			pCommand->m_pCapabilities->m_ShaderSupport = false;
			if(MinorV >= 1)
				pCommand->m_pCapabilities->m_ShaderSupport = true;

			int Texture3DSize = 0;
			glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &Texture3DSize);
			if(Texture3DSize >= 256)
			{
				pCommand->m_pCapabilities->m_3DTextures = true;
			}

			// check for array texture extension
			if(pCommand->m_pCapabilities->m_ShaderSupport && GLEW_EXT_texture_array)
			{
				int TextureLayers = 0;
				glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS_EXT, &TextureLayers);
				if(TextureLayers >= 256)
				{
					pCommand->m_pCapabilities->m_2DArrayTextures = true;
					pCommand->m_pCapabilities->m_2DArrayTexturesAsExtension = true;
				}
			}

			pCommand->m_pCapabilities->m_TileBuffering = pCommand->m_pCapabilities->m_2DArrayTextures || pCommand->m_pCapabilities->m_3DTextures;
			pCommand->m_pCapabilities->m_QuadBuffering = false;
			pCommand->m_pCapabilities->m_TextBuffering = false;
			pCommand->m_pCapabilities->m_QuadContainerBuffering = false;

			if(GLEW_ARB_texture_non_power_of_two || pCommand->m_GlewMajor > 2)
				pCommand->m_pCapabilities->m_NPOTTextures = true;
			else
			{
				pCommand->m_pCapabilities->m_NPOTTextures = false;
			}

			if(!pCommand->m_pCapabilities->m_NPOTTextures || (!pCommand->m_pCapabilities->m_3DTextures && !pCommand->m_pCapabilities->m_2DArrayTextures))
			{
				*pCommand->m_pInitError = -2;
				pCommand->m_pCapabilities->m_ContextMajor = 1;
				pCommand->m_pCapabilities->m_ContextMinor = 5;
				pCommand->m_pCapabilities->m_ContextPatch = 0;
			}
		}
		else if(MajorV < 2)
		{
			pCommand->m_pCapabilities->m_TileBuffering = false;
			pCommand->m_pCapabilities->m_QuadBuffering = false;
			pCommand->m_pCapabilities->m_TextBuffering = false;
			pCommand->m_pCapabilities->m_QuadContainerBuffering = false;
			pCommand->m_pCapabilities->m_ShaderSupport = false;

			pCommand->m_pCapabilities->m_MipMapping = false;
			pCommand->m_pCapabilities->m_3DTextures = false;
			pCommand->m_pCapabilities->m_2DArrayTextures = false;
			pCommand->m_pCapabilities->m_NPOTTextures = false;
		}
	}
}

void CCommandProcessorFragment_SDL::Cmd_Update_Viewport(const SCommand_Update_Viewport *pCommand)
{
	glViewport(pCommand->m_X, pCommand->m_Y, pCommand->m_Width, pCommand->m_Height);
}

void CCommandProcessorFragment_SDL::Cmd_Shutdown(const SCommand_Shutdown *pCommand)
{
	SDL_GL_MakeCurrent(NULL, NULL);
}

void CCommandProcessorFragment_SDL::Cmd_Swap(const CCommandBuffer::SCommand_Swap *pCommand)
{
	SDL_GL_SwapWindow(m_pWindow);

	if(pCommand->m_Finish)
		glFinish();
}

void CCommandProcessorFragment_SDL::Cmd_VSync(const CCommandBuffer::SCommand_VSync *pCommand)
{
	*pCommand->m_pRetOk = SDL_GL_SetSwapInterval(pCommand->m_VSync) == 0;
}

void CCommandProcessorFragment_SDL::Cmd_Resize(const CCommandBuffer::SCommand_Resize *pCommand)
{
	glViewport(0, 0, pCommand->m_Width, pCommand->m_Height);
}

void CCommandProcessorFragment_SDL::Cmd_VideoModes(const CCommandBuffer::SCommand_VideoModes *pCommand)
{
	SDL_DisplayMode mode;
	int maxModes = SDL_GetNumDisplayModes(pCommand->m_Screen),
	    numModes = 0;

	for(int i = 0; i < maxModes; i++)
	{
		if(SDL_GetDisplayMode(pCommand->m_Screen, i, &mode) < 0)
		{
			dbg_msg("gfx", "unable to get display mode: %s", SDL_GetError());
			continue;
		}

		bool AlreadyFound = false;
		for(int j = 0; j < numModes; j++)
		{
			if(pCommand->m_pModes[j].m_Width == mode.w && pCommand->m_pModes[j].m_Height == mode.h)
			{
				AlreadyFound = true;
				break;
			}
		}
		if(AlreadyFound)
			continue;

		pCommand->m_pModes[numModes].m_Width = mode.w;
		pCommand->m_pModes[numModes].m_Height = mode.h;
		pCommand->m_pModes[numModes].m_Red = 8;
		pCommand->m_pModes[numModes].m_Green = 8;
		pCommand->m_pModes[numModes].m_Blue = 8;
		numModes++;
	}
	*pCommand->m_pNumModes = numModes;
}

CCommandProcessorFragment_SDL::CCommandProcessorFragment_SDL()
{
}

bool CCommandProcessorFragment_SDL::RunCommand(const CCommandBuffer::SCommand *pBaseCommand)
{
	switch(pBaseCommand->m_Cmd)
	{
	case CCommandBuffer::CMD_SWAP: Cmd_Swap(static_cast<const CCommandBuffer::SCommand_Swap *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_VSYNC: Cmd_VSync(static_cast<const CCommandBuffer::SCommand_VSync *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RESIZE: Cmd_Resize(static_cast<const CCommandBuffer::SCommand_Resize *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_VIDEOMODES: Cmd_VideoModes(static_cast<const CCommandBuffer::SCommand_VideoModes *>(pBaseCommand)); break;
	case CMD_INIT: Cmd_Init(static_cast<const SCommand_Init *>(pBaseCommand)); break;
	case CMD_SHUTDOWN: Cmd_Shutdown(static_cast<const SCommand_Shutdown *>(pBaseCommand)); break;
	case CMD_UPDATE_VIEWPORT: Cmd_Update_Viewport(static_cast<const SCommand_Update_Viewport *>(pBaseCommand)); break;
	default: return false;
	}

	return true;
}

// ------------ CCommandProcessor_SDL_OpenGL

void CCommandProcessor_SDL_OpenGL::RunBuffer(CCommandBuffer *pBuffer)
{
	for(CCommandBuffer::SCommand *pCommand = pBuffer->Head(); pCommand; pCommand = pCommand->m_pNext)
	{
		if(m_pOpenGL->RunCommand(pCommand))
			continue;

		if(m_SDL.RunCommand(pCommand))
			continue;

		if(m_General.RunCommand(pCommand))
			continue;

		dbg_msg("gfx", "unknown command %d", pCommand->m_Cmd);
	}
}

CCommandProcessor_SDL_OpenGL::CCommandProcessor_SDL_OpenGL(int OpenGLMajor, int OpenGLMinor, int OpenGLPatch)
{
	if(OpenGLMajor < 2)
	{
		m_pOpenGL = new CCommandProcessorFragment_OpenGL();
	}
	if(OpenGLMajor == 2)
	{
		m_pOpenGL = new CCommandProcessorFragment_OpenGL2();
	}
	if(OpenGLMajor == 3 && OpenGLMinor == 0)
	{
		m_pOpenGL = new CCommandProcessorFragment_OpenGL3();
	}
	else if((OpenGLMajor == 3 && OpenGLMinor == 3) || OpenGLMajor >= 4)
	{
		m_pOpenGL = new CCommandProcessorFragment_OpenGL3_3();
	}
}

CCommandProcessor_SDL_OpenGL::~CCommandProcessor_SDL_OpenGL()
{
	delete m_pOpenGL;
}

// ------------ CGraphicsBackend_SDL_OpenGL

static void GetGlewVersion(int &GlewMajor, int &GlewMinor, int &GlewPatch)
{
#ifdef GLEW_VERSION_4_6
	if(GLEW_VERSION_4_6)
	{
		GlewMajor = 4;
		GlewMinor = 6;
		GlewPatch = 0;
		return;
	}
#endif
	if(GLEW_VERSION_4_5)
	{
		GlewMajor = 4;
		GlewMinor = 5;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_4_4)
	{
		GlewMajor = 4;
		GlewMinor = 4;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_4_3)
	{
		GlewMajor = 4;
		GlewMinor = 3;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_4_2)
	{
		GlewMajor = 4;
		GlewMinor = 2;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_4_1)
	{
		GlewMajor = 4;
		GlewMinor = 1;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_4_0)
	{
		GlewMajor = 4;
		GlewMinor = 0;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_3_3)
	{
		GlewMajor = 3;
		GlewMinor = 3;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_3_0)
	{
		GlewMajor = 3;
		GlewMinor = 0;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_2_1)
	{
		GlewMajor = 2;
		GlewMinor = 1;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_2_0)
	{
		GlewMajor = 2;
		GlewMinor = 0;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_1_5)
	{
		GlewMajor = 1;
		GlewMinor = 5;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_1_4)
	{
		GlewMajor = 1;
		GlewMinor = 4;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_1_3)
	{
		GlewMajor = 1;
		GlewMinor = 3;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_1_2_1)
	{
		GlewMajor = 1;
		GlewMinor = 2;
		GlewPatch = 1;
		return;
	}
	if(GLEW_VERSION_1_2)
	{
		GlewMajor = 1;
		GlewMinor = 2;
		GlewPatch = 0;
		return;
	}
	if(GLEW_VERSION_1_1)
	{
		GlewMajor = 1;
		GlewMinor = 1;
		GlewPatch = 0;
		return;
	}
}

static int IsVersionSupportedGlew(int VersionMajor, int VersionMinor, int VersionPatch, int GlewMajor, int GlewMinor, int GlewPatch)
{
	int InitError = 0;
	if(VersionMajor >= 4 && GlewMajor < 4)
	{
		InitError = -1;
	}
	else if(VersionMajor >= 3 && GlewMajor < 3)
	{
		InitError = -1;
	}
	else if(VersionMajor == 3 && GlewMajor == 3)
	{
		if(VersionMinor >= 3 && GlewMinor < 3)
		{
			InitError = -1;
		}
		if(VersionMinor >= 2 && GlewMinor < 2)
		{
			InitError = -1;
		}
		if(VersionMinor >= 1 && GlewMinor < 1)
		{
			InitError = -1;
		}
		if(VersionMinor >= 0 && GlewMinor < 0)
		{
			InitError = -1;
		}
	}
	else if(VersionMajor >= 2 && GlewMajor < 2)
	{
		InitError = -1;
	}
	else if(VersionMajor == 2 && GlewMajor == 2)
	{
		if(VersionMinor >= 1 && GlewMinor < 1)
		{
			InitError = -1;
		}
		if(VersionMinor >= 0 && GlewMinor < 0)
		{
			InitError = -1;
		}
	}
	else if(VersionMajor >= 1 && GlewMajor < 1)
	{
		InitError = -1;
	}
	else if(VersionMajor == 1 && GlewMajor == 1)
	{
		if(VersionMinor >= 5 && GlewMinor < 5)
		{
			InitError = -1;
		}
		if(VersionMinor >= 4 && GlewMinor < 4)
		{
			InitError = -1;
		}
		if(VersionMinor >= 3 && GlewMinor < 3)
		{
			InitError = -1;
		}
		if(VersionMinor >= 2 && GlewMinor < 2)
		{
			InitError = -1;
		}
		else if(VersionMinor == 2 && GlewMinor == 2)
		{
			if(VersionPatch >= 1 && GlewPatch < 1)
			{
				InitError = -1;
			}
			if(VersionPatch >= 0 && GlewPatch < 0)
			{
				InitError = -1;
			}
		}
		if(VersionMinor >= 1 && GlewMinor < 1)
		{
			InitError = -1;
		}
		if(VersionMinor >= 0 && GlewMinor < 0)
		{
			InitError = -1;
		}
	}

	return InitError;
}

int CGraphicsBackend_SDL_OpenGL::Init(const char *pName, int *Screen, int *pWidth, int *pHeight, int FsaaSamples, int Flags, int *pDesktopWidth, int *pDesktopHeight, int *pCurrentWidth, int *pCurrentHeight, IStorage *pStorage)
{
	// print sdl version
	{
		SDL_version Compiled;
		SDL_version Linked;

		SDL_VERSION(&Compiled);
		SDL_GetVersion(&Linked);
		dbg_msg("sdl", "SDL version %d.%d.%d (compiled = %d.%d.%d)", Linked.major, Linked.minor, Linked.patch,
			Compiled.major, Compiled.minor, Compiled.patch);
	}

	if(!SDL_WasInit(SDL_INIT_VIDEO))
	{
		if(SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
		{
			dbg_msg("gfx", "unable to init SDL video: %s", SDL_GetError());
			return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_SDL_INIT_FAILED;
		}
	}

	SDL_ClearError();
	const char *pErr = NULL;

	// Query default values, since they are platform dependent
	static bool s_InitDefaultParams = false;
	static int s_SDLGLContextProfileMask, s_SDLGLContextMajorVersion, s_SDLGLContextMinorVersion;
	static bool s_TriedOpenGL3Context = false;

	if(!s_InitDefaultParams)
	{
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &s_SDLGLContextProfileMask);
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &s_SDLGLContextMajorVersion);
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &s_SDLGLContextMinorVersion);
		s_InitDefaultParams = true;
	}

	//clamp the versions to existing versions(only for OpenGL major <= 3)
	if(g_Config.m_GfxOpenGLMajor == 1)
	{
		g_Config.m_GfxOpenGLMinor = clamp(g_Config.m_GfxOpenGLMinor, 1, 5);
		if(g_Config.m_GfxOpenGLMinor == 2)
			g_Config.m_GfxOpenGLPatch = clamp(g_Config.m_GfxOpenGLPatch, 0, 1);
		else
			g_Config.m_GfxOpenGLPatch = 0;
	}
	else if(g_Config.m_GfxOpenGLMajor == 2)
	{
		g_Config.m_GfxOpenGLMinor = clamp(g_Config.m_GfxOpenGLMinor, 0, 1);
		g_Config.m_GfxOpenGLPatch = 0;
	}
	else if(g_Config.m_GfxOpenGLMajor == 3)
	{
		g_Config.m_GfxOpenGLMinor = clamp(g_Config.m_GfxOpenGLMinor, 0, 3);
		if(g_Config.m_GfxOpenGLMinor < 3)
			g_Config.m_GfxOpenGLMinor = 0;
		g_Config.m_GfxOpenGLPatch = 0;
	}

	// if OpenGL3 context was tried to be created, but failed, we have to restore the old context attributes
	bool IsNewOpenGL = (g_Config.m_GfxOpenGLMajor == 3 && g_Config.m_GfxOpenGLMinor == 3) || g_Config.m_GfxOpenGLMajor >= 4;
	if(s_TriedOpenGL3Context && !IsNewOpenGL)
	{
		s_TriedOpenGL3Context = false;

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, s_SDLGLContextProfileMask);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, s_SDLGLContextMajorVersion);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, s_SDLGLContextMinorVersion);
	}

	m_UseNewOpenGL = false;
	if(IsNewOpenGL)
	{
		s_TriedOpenGL3Context = true;

		if(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0)
		{
			pErr = SDL_GetError();
			if(pErr[0] != '\0')
			{
				dbg_msg("gfx", "Using old OpenGL context, because an error occurred while trying to use OpenGL context %zu.%zu: %s.", (size_t)g_Config.m_GfxOpenGLMajor, (size_t)g_Config.m_GfxOpenGLMinor, pErr);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, s_SDLGLContextProfileMask);
			}
			else
			{
				if(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, g_Config.m_GfxOpenGLMajor) == 0 && SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, g_Config.m_GfxOpenGLMinor) == 0)
				{
					pErr = SDL_GetError();
					if(pErr[0] != '\0')
					{
						dbg_msg("gfx", "Using old OpenGL context, because an error occurred while trying to use OpenGL context %zu.%zu: %s.", (size_t)g_Config.m_GfxOpenGLMajor, (size_t)g_Config.m_GfxOpenGLMinor, pErr);
						SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, s_SDLGLContextMajorVersion);
						SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, s_SDLGLContextMinorVersion);
					}
					else
					{
						m_UseNewOpenGL = true;
						int vMaj, vMin;
						SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &vMaj);
						SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &vMin);
						dbg_msg("gfx", "Using OpenGL version %d.%d.", vMaj, vMin);
					}
				}
				else
				{
					dbg_msg("gfx", "Couldn't create OpenGL %zu.%zu context.", (size_t)g_Config.m_GfxOpenGLMajor, (size_t)g_Config.m_GfxOpenGLMinor);
					SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, s_SDLGLContextMajorVersion);
					SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, s_SDLGLContextMinorVersion);
				}
			}
		}
		else
		{
			//set default attributes
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, s_SDLGLContextProfileMask);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, s_SDLGLContextMajorVersion);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, s_SDLGLContextMinorVersion);
		}
	}
	//if non standard opengl, set it
	else if(s_SDLGLContextMajorVersion != g_Config.m_GfxOpenGLMajor || s_SDLGLContextMinorVersion != g_Config.m_GfxOpenGLMinor)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, g_Config.m_GfxOpenGLMajor);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, g_Config.m_GfxOpenGLMinor);
		dbg_msg("gfx", "Created OpenGL %zu.%zu context.", (size_t)g_Config.m_GfxOpenGLMajor, (size_t)g_Config.m_GfxOpenGLMinor);

		if(g_Config.m_GfxOpenGLMajor == 3 && g_Config.m_GfxOpenGLMinor == 0)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
		}
	}

	// set screen
	SDL_Rect ScreenPos;
	m_NumScreens = SDL_GetNumVideoDisplays();
	if(m_NumScreens > 0)
	{
		if(*Screen < 0 || *Screen >= m_NumScreens)
			*Screen = 0;
		if(SDL_GetDisplayBounds(*Screen, &ScreenPos) != 0)
		{
			dbg_msg("gfx", "unable to retrieve screen information: %s", SDL_GetError());
			return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_SDL_SCREEN_INFO_REQUEST_FAILED;
		}
	}
	else
	{
		dbg_msg("gfx", "unable to retrieve number of screens: %s", SDL_GetError());
		return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_SDL_SCREEN_REQUEST_FAILED;
	}

	// store desktop resolution for settings reset button
	SDL_DisplayMode DisplayMode;
	if(SDL_GetDesktopDisplayMode(*Screen, &DisplayMode))
	{
		dbg_msg("gfx", "unable to get desktop resolution: %s", SDL_GetError());
		return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_SDL_SCREEN_RESOLUTION_REQUEST_FAILED;
	}
	*pDesktopWidth = DisplayMode.w;
	*pDesktopHeight = DisplayMode.h;

	// use desktop resolution as default resolution
	if(*pWidth == 0 || *pHeight == 0)
	{
		*pWidth = *pDesktopWidth;
		*pHeight = *pDesktopHeight;
	}

	// set flags
	int SdlFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_INPUT_GRABBED | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS;
	if(Flags & IGraphicsBackend::INITFLAG_HIGHDPI)
		SdlFlags |= SDL_WINDOW_ALLOW_HIGHDPI;
	if(Flags & IGraphicsBackend::INITFLAG_RESIZABLE)
		SdlFlags |= SDL_WINDOW_RESIZABLE;
	if(Flags & IGraphicsBackend::INITFLAG_BORDERLESS)
		SdlFlags |= SDL_WINDOW_BORDERLESS;
	if(Flags & IGraphicsBackend::INITFLAG_FULLSCREEN)
	{
		// when we are at fullscreen, we really shouldn't allow window sizes, that aren't supported by the driver
		bool SupportedResolution = false;
		SDL_DisplayMode mode;
		int maxModes = SDL_GetNumDisplayModes(g_Config.m_GfxScreen);

		for(int i = 0; i < maxModes; i++)
		{
			if(SDL_GetDisplayMode(g_Config.m_GfxScreen, i, &mode) < 0)
			{
				dbg_msg("gfx", "unable to get display mode: %s", SDL_GetError());
				continue;
			}

			if(*pWidth == mode.w && *pHeight == mode.h)
			{
				SupportedResolution = true;
				break;
			}
		}
		if(SupportedResolution)
			SdlFlags |= SDL_WINDOW_FULLSCREEN;
		else
			SdlFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	else if(Flags & (IGraphicsBackend::INITFLAG_DESKTOP_FULLSCREEN))
	{
		SdlFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	// set gl attributes
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	if(FsaaSamples)
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, FsaaSamples);
	}
	else
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	}

	if(g_Config.m_InpMouseOld)
		SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1");

	m_pWindow = SDL_CreateWindow(
		pName,
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		*pWidth,
		*pHeight,
		SdlFlags);

	// set caption
	if(m_pWindow == NULL)
	{
		dbg_msg("gfx", "unable to create window: %s", SDL_GetError());
		return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_SDL_WINDOW_CREATE_FAILED;
	}

	m_GLContext = SDL_GL_CreateContext(m_pWindow);

	if(m_GLContext == NULL)
	{
		dbg_msg("gfx", "unable to create OpenGL context: %s", SDL_GetError());
		return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_OPENGL_CONTEXT_FAILED;
	}

	//support graphic cards that are pretty old(and linux)
	glewExperimental = GL_TRUE;
	if(GLEW_OK != glewInit())
		return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_UNKNOWN;

	int GlewMajor = 0;
	int GlewMinor = 0;
	int GlewPatch = 0;

	GetGlewVersion(GlewMajor, GlewMinor, GlewPatch);

	int InitError = 0;
	const char *pErrorStr = NULL;

	InitError = IsVersionSupportedGlew(g_Config.m_GfxOpenGLMajor, g_Config.m_GfxOpenGLMinor, g_Config.m_GfxOpenGLPatch, GlewMajor, GlewMinor, GlewPatch);

	SDL_GL_GetDrawableSize(m_pWindow, pCurrentWidth, pCurrentHeight);
	SDL_GL_SetSwapInterval(Flags & IGraphicsBackend::INITFLAG_VSYNC ? 1 : 0);
	SDL_GL_MakeCurrent(NULL, NULL);

	if(InitError != 0)
	{
		SDL_GL_DeleteContext(m_GLContext);
		SDL_DestroyWindow(m_pWindow);

		// try setting to glew supported version
		g_Config.m_GfxOpenGLMajor = GlewMajor;
		g_Config.m_GfxOpenGLMinor = GlewMinor;
		g_Config.m_GfxOpenGLPatch = GlewPatch;

		return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_OPENGL_VERSION_FAILED;
	}

	// start the command processor
	m_pProcessor = new CCommandProcessor_SDL_OpenGL(g_Config.m_GfxOpenGLMajor, g_Config.m_GfxOpenGLMinor, g_Config.m_GfxOpenGLPatch);
	StartProcessor(m_pProcessor);

	mem_zero(m_aErrorString, sizeof(m_aErrorString) / sizeof(m_aErrorString[0]));

	// issue init commands for OpenGL and SDL
	CCommandBuffer CmdBuffer(1024, 512);
	//run sdl first to have the context in the thread
	CCommandProcessorFragment_SDL::SCommand_Init CmdSDL;
	CmdSDL.m_pWindow = m_pWindow;
	CmdSDL.m_GLContext = m_GLContext;
	CmdSDL.m_pCapabilities = &m_Capabilites;
	CmdSDL.m_RequestedMajor = g_Config.m_GfxOpenGLMajor;
	CmdSDL.m_RequestedMinor = g_Config.m_GfxOpenGLMinor;
	CmdSDL.m_RequestedPatch = g_Config.m_GfxOpenGLPatch;
	CmdSDL.m_GlewMajor = GlewMajor;
	CmdSDL.m_GlewMinor = GlewMinor;
	CmdSDL.m_GlewPatch = GlewPatch;
	CmdSDL.m_pInitError = &InitError;
	CmdSDL.m_pErrStringPtr = &pErrorStr;
	CmdSDL.m_pVendorString = m_aVendorString;
	CmdSDL.m_pVersionString = m_aVersionString;
	CmdSDL.m_pRendererString = m_aRendererString;
	CmdBuffer.AddCommandUnsafe(CmdSDL);
	RunBuffer(&CmdBuffer);
	WaitForIdle();
	CmdBuffer.Reset();

	if(InitError == 0)
	{
		CCommandProcessorFragment_OpenGL::SCommand_Init CmdOpenGL;
		CmdOpenGL.m_pTextureMemoryUsage = &m_TextureMemoryUsage;
		CmdOpenGL.m_pStorage = pStorage;
		CmdOpenGL.m_pCapabilities = &m_Capabilites;
		CmdOpenGL.m_pInitError = &InitError;
		CmdBuffer.AddCommandUnsafe(CmdOpenGL);
		RunBuffer(&CmdBuffer);
		WaitForIdle();
		CmdBuffer.Reset();

		if(InitError == -2)
		{
			CCommandProcessorFragment_OpenGL::SCommand_Shutdown CmdGL;
			CmdBuffer.AddCommandUnsafe(CmdGL);
			RunBuffer(&CmdBuffer);
			WaitForIdle();
			CmdBuffer.Reset();

			g_Config.m_GfxOpenGLMajor = 1;
			g_Config.m_GfxOpenGLMinor = 5;
			g_Config.m_GfxOpenGLPatch = 0;
		}
	}

	if(InitError != 0)
	{
		CCommandProcessorFragment_SDL::SCommand_Shutdown Cmd;
		CmdBuffer.AddCommandUnsafe(Cmd);
		RunBuffer(&CmdBuffer);
		WaitForIdle();
		CmdBuffer.Reset();

		// stop and delete the processor
		StopProcessor();
		delete m_pProcessor;
		m_pProcessor = 0;

		SDL_GL_DeleteContext(m_GLContext);
		SDL_DestroyWindow(m_pWindow);

		// try setting to version string's supported version
		if(InitError == -2)
		{
			g_Config.m_GfxOpenGLMajor = m_Capabilites.m_ContextMajor;
			g_Config.m_GfxOpenGLMinor = m_Capabilites.m_ContextMinor;
			g_Config.m_GfxOpenGLPatch = m_Capabilites.m_ContextPatch;
		}

		if(pErrorStr != NULL)
		{
			str_copy(m_aErrorString, pErrorStr, sizeof(m_aErrorString) / sizeof(m_aErrorString[0]));
		}

		return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_OPENGL_VERSION_FAILED;
	}

	if(SetWindowScreen(g_Config.m_GfxScreen))
	{
		// query the current displaymode, when running in fullscreen
		// this is required if DPI scaling is active
		if(SdlFlags & SDL_WINDOW_FULLSCREEN)
		{
			SDL_DisplayMode CurrentDisplayMode;
			SDL_GetCurrentDisplayMode(g_Config.m_GfxScreen, &CurrentDisplayMode);

			*pCurrentWidth = CurrentDisplayMode.w;
			*pCurrentHeight = CurrentDisplayMode.h;

			// since the window is centered, calculate how much the viewport has to be fixed
			//int XOverflow = (*pWidth > *pCurrentWidth ? (*pWidth - *pCurrentWidth) : 0);
			//int YOverflow = (*pHeight > *pCurrentHeight ? (*pHeight - *pCurrentHeight) : 0);
			//TODO: current problem is, that the opengl driver knows about the scaled display,
			//so the viewport cannot be adjusted for resolutions, that are higher than allowed by the display driver

			CCommandProcessorFragment_SDL::SCommand_Update_Viewport CmdSDL;
			CmdSDL.m_X = 0;
			CmdSDL.m_Y = 0;

			CmdSDL.m_Width = CurrentDisplayMode.w;
			CmdSDL.m_Height = CurrentDisplayMode.h;
			CmdBuffer.AddCommandUnsafe(CmdSDL);
			RunBuffer(&CmdBuffer);
			WaitForIdle();
			CmdBuffer.Reset();
		}
	}

	// return
	return EGraphicsBackendErrorCodes::GRAPHICS_BACKEND_ERROR_CODE_NONE;
}

int CGraphicsBackend_SDL_OpenGL::Shutdown()
{
	// issue a shutdown command
	CCommandBuffer CmdBuffer(1024, 512);
	CCommandProcessorFragment_OpenGL::SCommand_Shutdown CmdGL;
	CmdBuffer.AddCommandUnsafe(CmdGL);
	RunBuffer(&CmdBuffer);
	WaitForIdle();
	CmdBuffer.Reset();

	CCommandProcessorFragment_SDL::SCommand_Shutdown Cmd;
	CmdBuffer.AddCommandUnsafe(Cmd);
	RunBuffer(&CmdBuffer);
	WaitForIdle();
	CmdBuffer.Reset();

	// stop and delete the processor
	StopProcessor();
	delete m_pProcessor;
	m_pProcessor = 0;

	SDL_GL_DeleteContext(m_GLContext);
	SDL_DestroyWindow(m_pWindow);

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	return 0;
}

int CGraphicsBackend_SDL_OpenGL::MemoryUsage() const
{
	return m_TextureMemoryUsage;
}

void CGraphicsBackend_SDL_OpenGL::Minimize()
{
	SDL_MinimizeWindow(m_pWindow);
}

void CGraphicsBackend_SDL_OpenGL::Maximize()
{
	// TODO: SDL
}

void CGraphicsBackend_SDL_OpenGL::SetWindowParams(int FullscreenMode, bool IsBorderless)
{
	if(FullscreenMode > 0)
	{
		if(FullscreenMode == 1)
		{
#if defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_HAIKU) // Todo SDL: remove this when fixed (game freezes when losing focus in fullscreen)
			SDL_SetWindowFullscreen(m_pWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
#else
			SDL_SetWindowFullscreen(m_pWindow, SDL_WINDOW_FULLSCREEN);
#endif
		}
		else if(FullscreenMode == 2)
		{
			SDL_SetWindowFullscreen(m_pWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
		}
	}
	else
	{
		SDL_SetWindowFullscreen(m_pWindow, 0);
		SDL_SetWindowBordered(m_pWindow, SDL_bool(!IsBorderless));
	}
}

bool CGraphicsBackend_SDL_OpenGL::SetWindowScreen(int Index)
{
	if(Index >= 0 && Index < m_NumScreens)
	{
		SDL_Rect ScreenPos;
		if(SDL_GetDisplayBounds(Index, &ScreenPos) == 0)
		{
			SDL_SetWindowPosition(m_pWindow,
				SDL_WINDOWPOS_CENTERED_DISPLAY(Index),
				SDL_WINDOWPOS_CENTERED_DISPLAY(Index));
			return true;
		}
	}

	return false;
}

int CGraphicsBackend_SDL_OpenGL::GetWindowScreen()
{
	return SDL_GetWindowDisplayIndex(m_pWindow);
}

int CGraphicsBackend_SDL_OpenGL::WindowActive()
{
	return SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_INPUT_FOCUS;
}

int CGraphicsBackend_SDL_OpenGL::WindowOpen()
{
	return SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_SHOWN;
}

void CGraphicsBackend_SDL_OpenGL::SetWindowGrab(bool Grab)
{
	SDL_SetWindowGrab(m_pWindow, Grab ? SDL_TRUE : SDL_FALSE);
}

void CGraphicsBackend_SDL_OpenGL::ResizeWindow(int w, int h)
{
	SDL_SetWindowSize(m_pWindow, w, h);
}

void CGraphicsBackend_SDL_OpenGL::GetViewportSize(int &w, int &h)
{
	SDL_GL_GetDrawableSize(m_pWindow, &w, &h);
}

void CGraphicsBackend_SDL_OpenGL::NotifyWindow()
{
	// get window handle
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if(!SDL_GetWindowWMInfo(m_pWindow, &info))
	{
		dbg_msg("gfx", "unable to obtain window handle");
		return;
	}

#if defined(CONF_FAMILY_WINDOWS)
	FLASHWINFO desc;
	desc.cbSize = sizeof(desc);
	desc.hwnd = info.info.win.window;
	desc.dwFlags = FLASHW_TRAY;
	desc.uCount = 3; // flash 3 times
	desc.dwTimeout = 0;

	FlashWindowEx(&desc);
#elif defined(SDL_VIDEO_DRIVER_X11) && !defined(CONF_PLATFORM_MACOS)
	Display *dpy = info.info.x11.display;
	Window win = info.info.x11.window;

	// Old hints
	XWMHints *wmhints;
	wmhints = XAllocWMHints();
	wmhints->flags = XUrgencyHint;
	XSetWMHints(dpy, win, wmhints);
	XFree(wmhints);

	// More modern way of notifying
	static Atom demandsAttention = XInternAtom(dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", true);
	static Atom wmState = XInternAtom(dpy, "_NET_WM_STATE", true);
	XChangeProperty(dpy, win, wmState, XA_ATOM, 32, PropModeReplace,
		(unsigned char *)&demandsAttention, 1);
#endif
}

IGraphicsBackend *CreateGraphicsBackend() { return new CGraphicsBackend_SDL_OpenGL; }
