

// ------------ CCommandProcessorFragment_OpenGL

int CCommandProcessorFragment_OpenGL::TexFormatToOpenGLFormat(int TexFormat)
{
	if(TexFormat == CCommandBuffer::TEXFORMAT_RGB)
		return GL_RGB;
	if(TexFormat == CCommandBuffer::TEXFORMAT_ALPHA)
		return GL_ALPHA;
	if(TexFormat == CCommandBuffer::TEXFORMAT_RGBA)
		return GL_RGBA;
	return GL_RGBA;
}

int CCommandProcessorFragment_OpenGL::TexFormatToImageColorChannelCount(int TexFormat)
{
	if(TexFormat == CCommandBuffer::TEXFORMAT_RGB)
		return 3;
	if(TexFormat == CCommandBuffer::TEXFORMAT_ALPHA)
		return 1;
	if(TexFormat == CCommandBuffer::TEXFORMAT_RGBA)
		return 4;
	return 4;
}

void *CCommandProcessorFragment_OpenGL::Resize(int Width, int Height, int NewWidth, int NewHeight, int Format, const unsigned char *pData)
{
	int Bpp = TexFormatToImageColorChannelCount(Format);

	return ResizeImage((const uint8_t *)pData, Width, Height, NewWidth, NewHeight, Bpp);
}

bool CCommandProcessorFragment_OpenGL::IsTexturedState(const CCommandBuffer::SState &State)
{
	return State.m_Texture >= 0 && State.m_Texture < (int)m_Textures.size();
}

void CCommandProcessorFragment_OpenGL::SetState(const CCommandBuffer::SState &State, bool Use2DArrayTextures)
{
	// blend
	switch(State.m_BlendMode)
	{
	case CCommandBuffer::BLEND_NONE:
		glDisable(GL_BLEND);
		break;
	case CCommandBuffer::BLEND_ALPHA:
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		break;
	case CCommandBuffer::BLEND_ADDITIVE:
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		break;
	default:
		dbg_msg("render", "unknown blendmode %d\n", State.m_BlendMode);
	};
	m_LastBlendMode = State.m_BlendMode;

	// clip
	if(State.m_ClipEnable)
	{
		glScissor(State.m_ClipX, State.m_ClipY, State.m_ClipW, State.m_ClipH);
		glEnable(GL_SCISSOR_TEST);
		m_LastClipEnable = true;
	}
	else if(m_LastClipEnable)
	{
		// Don't disable it always
		glDisable(GL_SCISSOR_TEST);
		m_LastClipEnable = false;
	}

	glDisable(GL_TEXTURE_2D);
	if(!m_HasShaders)
	{
		if(m_Has3DTextures)
			glDisable(GL_TEXTURE_3D);
		if(m_Has2DArrayTextures)
		{
			glDisable(m_2DArrayTarget);
		}
	}

	if(m_HasShaders && IsNewApi())
	{
		glBindSampler(0, 0);
	}

	// texture
	if(IsTexturedState(State))
	{
		if(!Use2DArrayTextures)
		{
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, m_Textures[State.m_Texture].m_Tex);

			if(m_Textures[State.m_Texture].m_LastWrapMode != State.m_WrapMode)
			{
				switch(State.m_WrapMode)
				{
				case CCommandBuffer::WRAP_REPEAT:
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
					break;
				case CCommandBuffer::WRAP_CLAMP:
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					break;
				default:
					dbg_msg("render", "unknown wrapmode %d\n", State.m_WrapMode);
				};
				m_Textures[State.m_Texture].m_LastWrapMode = State.m_WrapMode;
			}
		}
		else
		{
			if(m_Has2DArrayTextures)
			{
				if(!m_HasShaders)
					glEnable(m_2DArrayTarget);
				glBindTexture(m_2DArrayTarget, m_Textures[State.m_Texture].m_Tex2DArray);
			}
			else if(m_Has3DTextures)
			{
				if(!m_HasShaders)
					glEnable(GL_TEXTURE_3D);
				glBindTexture(GL_TEXTURE_3D, m_Textures[State.m_Texture].m_Tex2DArray);
			}
			else
			{
				dbg_msg("opengl", "Error: this call should not happen.");
			}
		}
	}

	// screen mapping
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(State.m_ScreenTL.x, State.m_ScreenBR.x, State.m_ScreenBR.y, State.m_ScreenTL.y, -10.0f, 10.f);
}

void CCommandProcessorFragment_OpenGL::Cmd_Init(const SCommand_Init *pCommand)
{
	m_pTextureMemoryUsage = pCommand->m_pTextureMemoryUsage;
	m_pTextureMemoryUsage->store(0, std::memory_order_relaxed);
	m_MaxTexSize = -1;

	m_OpenGLTextureLodBIAS = 0;

	m_Has2DArrayTextures = pCommand->m_pCapabilities->m_2DArrayTextures;
	if(pCommand->m_pCapabilities->m_2DArrayTexturesAsExtension)
	{
		m_Has2DArrayTexturesAsExtension = true;
		m_2DArrayTarget = GL_TEXTURE_2D_ARRAY_EXT;
	}
	else
	{
		m_Has2DArrayTexturesAsExtension = false;
		m_2DArrayTarget = GL_TEXTURE_2D_ARRAY;
	}

	m_Has3DTextures = pCommand->m_pCapabilities->m_3DTextures;
	m_HasMipMaps = pCommand->m_pCapabilities->m_MipMapping;
	m_HasNPOTTextures = pCommand->m_pCapabilities->m_NPOTTextures;

	m_LastBlendMode = CCommandBuffer::BLEND_ALPHA;
	m_LastClipEnable = false;
}

void CCommandProcessorFragment_OpenGL::Cmd_Texture_Update(const CCommandBuffer::SCommand_Texture_Update *pCommand)
{
	glBindTexture(GL_TEXTURE_2D, m_Textures[pCommand->m_Slot].m_Tex);

	void *pTexData = pCommand->m_pData;
	int Width = pCommand->m_Width;
	int Height = pCommand->m_Height;
	int X = pCommand->m_X;
	int Y = pCommand->m_Y;

	if(!m_HasNPOTTextures)
	{
		float ResizeW = m_Textures[pCommand->m_Slot].m_ResizeWidth;
		float ResizeH = m_Textures[pCommand->m_Slot].m_ResizeHeight;
		if(ResizeW > 0 && ResizeH > 0)
		{
			int ResizedW = (int)(Width * ResizeW);
			int ResizedH = (int)(Height * ResizeH);

			void *pTmpData = Resize(Width, Height, ResizedW, ResizedH, pCommand->m_Format, static_cast<const unsigned char *>(pTexData));
			free(pTexData);
			pTexData = pTmpData;

			Width = ResizedW;
			Height = ResizedH;
		}
	}

	if(m_Textures[pCommand->m_Slot].m_RescaleCount > 0)
	{
		int OldWidth = Width;
		int OldHeight = Height;
		for(int i = 0; i < m_Textures[pCommand->m_Slot].m_RescaleCount; ++i)
		{
			Width >>= 1;
			Height >>= 1;

			X /= 2;
			Y /= 2;
		}

		void *pTmpData = Resize(OldWidth, OldHeight, Width, Height, pCommand->m_Format, static_cast<const unsigned char *>(pTexData));
		free(pTexData);
		pTexData = pTmpData;
	}

	glTexSubImage2D(GL_TEXTURE_2D, 0, X, Y, Width, Height,
		TexFormatToOpenGLFormat(pCommand->m_Format), GL_UNSIGNED_BYTE, pTexData);
	free(pTexData);
}

void CCommandProcessorFragment_OpenGL::DestroyTexture(int Slot)
{
	m_pTextureMemoryUsage->store(m_pTextureMemoryUsage->load(std::memory_order_relaxed) - m_Textures[Slot].m_MemSize, std::memory_order_relaxed);

	if(m_Textures[Slot].m_Tex != 0)
	{
		glDeleteTextures(1, &m_Textures[Slot].m_Tex);
	}

	if(m_Textures[Slot].m_Tex2DArray != 0)
	{
		glDeleteTextures(1, &m_Textures[Slot].m_Tex2DArray);
	}

	if(IsNewApi())
	{
		if(m_Textures[Slot].m_Sampler != 0)
		{
			glDeleteSamplers(1, &m_Textures[Slot].m_Sampler);
		}
		if(m_Textures[Slot].m_Sampler2DArray != 0)
		{
			glDeleteSamplers(1, &m_Textures[Slot].m_Sampler2DArray);
		}
	}

	m_Textures[Slot].m_Tex = 0;
	m_Textures[Slot].m_Sampler = 0;
	m_Textures[Slot].m_Tex2DArray = 0;
	m_Textures[Slot].m_Sampler2DArray = 0;
	m_Textures[Slot].m_LastWrapMode = CCommandBuffer::WRAP_REPEAT;
}

void CCommandProcessorFragment_OpenGL::Cmd_Texture_Destroy(const CCommandBuffer::SCommand_Texture_Destroy *pCommand)
{
	DestroyTexture(pCommand->m_Slot);
}

void CCommandProcessorFragment_OpenGL::Cmd_Texture_Create(const CCommandBuffer::SCommand_Texture_Create *pCommand)
{
	int Width = pCommand->m_Width;
	int Height = pCommand->m_Height;
	void *pTexData = pCommand->m_pData;

	if(m_MaxTexSize == -1)
	{
		// fix the alignment to allow even 1byte changes, e.g. for alpha components
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_MaxTexSize);
	}

	if(pCommand->m_Slot >= (int)m_Textures.size())
		m_Textures.resize(m_Textures.size() * 2);

	m_Textures[pCommand->m_Slot].m_ResizeWidth = -1.f;
	m_Textures[pCommand->m_Slot].m_ResizeHeight = -1.f;

	if(!m_HasNPOTTextures)
	{
		int PowerOfTwoWidth = HighestBit(Width);
		int PowerOfTwoHeight = HighestBit(Height);
		if(Width != PowerOfTwoWidth || Height != PowerOfTwoHeight)
		{
			void *pTmpData = Resize(Width, Height, PowerOfTwoWidth, PowerOfTwoHeight, pCommand->m_Format, static_cast<const unsigned char *>(pTexData));
			free(pTexData);
			pTexData = pTmpData;

			m_Textures[pCommand->m_Slot].m_ResizeWidth = (float)PowerOfTwoWidth / (float)Width;
			m_Textures[pCommand->m_Slot].m_ResizeHeight = (float)PowerOfTwoHeight / (float)Height;

			Width = PowerOfTwoWidth;
			Height = PowerOfTwoHeight;
		}
	}

	int RescaleCount = 0;
	if(pCommand->m_Format == CCommandBuffer::TEXFORMAT_RGBA || pCommand->m_Format == CCommandBuffer::TEXFORMAT_RGB || pCommand->m_Format == CCommandBuffer::TEXFORMAT_ALPHA)
	{
		int OldWidth = Width;
		int OldHeight = Height;
		bool NeedsResize = false;

		if(Width > m_MaxTexSize || Height > m_MaxTexSize)
		{
			do
			{
				Width >>= 1;
				Height >>= 1;
				++RescaleCount;
			} while(Width > m_MaxTexSize || Height > m_MaxTexSize);
			NeedsResize = true;
		}
		else if(pCommand->m_Format != CCommandBuffer::TEXFORMAT_ALPHA && (Width > 16 && Height > 16 && (pCommand->m_Flags & CCommandBuffer::TEXFLAG_QUALITY) == 0))
		{
			Width >>= 1;
			Height >>= 1;
			++RescaleCount;
			NeedsResize = true;
		}

		if(NeedsResize)
		{
			void *pTmpData = Resize(OldWidth, OldHeight, Width, Height, pCommand->m_Format, static_cast<const unsigned char *>(pTexData));
			free(pTexData);
			pTexData = pTmpData;
		}
	}
	m_Textures[pCommand->m_Slot].m_Width = Width;
	m_Textures[pCommand->m_Slot].m_Height = Height;
	m_Textures[pCommand->m_Slot].m_RescaleCount = RescaleCount;

	int Oglformat = TexFormatToOpenGLFormat(pCommand->m_Format);
	int StoreOglformat = TexFormatToOpenGLFormat(pCommand->m_StoreFormat);

	if(pCommand->m_Flags & CCommandBuffer::TEXFLAG_COMPRESSED)
	{
		switch(StoreOglformat)
		{
		case GL_RGB: StoreOglformat = GL_COMPRESSED_RGB_ARB; break;
		case GL_ALPHA: StoreOglformat = GL_COMPRESSED_ALPHA_ARB; break;
		case GL_RGBA: StoreOglformat = GL_COMPRESSED_RGBA_ARB; break;
		default: StoreOglformat = GL_COMPRESSED_RGBA_ARB;
		}
	}

	if((pCommand->m_Flags & CCommandBuffer::TEXFLAG_NO_2D_TEXTURE) == 0)
	{
		glGenTextures(1, &m_Textures[pCommand->m_Slot].m_Tex);
		glBindTexture(GL_TEXTURE_2D, m_Textures[pCommand->m_Slot].m_Tex);
	}

	if(pCommand->m_Flags & CCommandBuffer::TEXFLAG_NOMIPMAPS || !m_HasMipMaps)
	{
		if((pCommand->m_Flags & CCommandBuffer::TEXFLAG_NO_2D_TEXTURE) == 0)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, StoreOglformat, Width, Height, 0, Oglformat, GL_UNSIGNED_BYTE, pTexData);
		}
	}
	else
	{
		if((pCommand->m_Flags & CCommandBuffer::TEXFLAG_NO_2D_TEXTURE) == 0)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
			if(m_OpenGLTextureLodBIAS != 0)
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, ((GLfloat)m_OpenGLTextureLodBIAS / 1000.0f));
			glTexImage2D(GL_TEXTURE_2D, 0, StoreOglformat, Width, Height, 0, Oglformat, GL_UNSIGNED_BYTE, pTexData);
		}

		int Flag2DArrayTexture = (CCommandBuffer::TEXFLAG_TO_2D_ARRAY_TEXTURE | CCommandBuffer::TEXFLAG_TO_2D_ARRAY_TEXTURE_SINGLE_LAYER);
		int Flag3DTexture = (CCommandBuffer::TEXFLAG_TO_3D_TEXTURE | CCommandBuffer::TEXFLAG_TO_3D_TEXTURE_SINGLE_LAYER);
		if((pCommand->m_Flags & (Flag2DArrayTexture | Flag3DTexture)) != 0)
		{
			bool Is3DTexture = (pCommand->m_Flags & Flag3DTexture) != 0;

			glGenTextures(1, &m_Textures[pCommand->m_Slot].m_Tex2DArray);

			GLenum Target = GL_TEXTURE_3D;

			if(Is3DTexture)
			{
				Target = GL_TEXTURE_3D;
			}
			else
			{
				Target = m_2DArrayTarget;
			}

			glBindTexture(Target, m_Textures[pCommand->m_Slot].m_Tex2DArray);

			if(IsNewApi())
			{
				glGenSamplers(1, &m_Textures[pCommand->m_Slot].m_Sampler2DArray);
				glBindSampler(0, m_Textures[pCommand->m_Slot].m_Sampler2DArray);
			}

			glTexParameteri(Target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			if(Is3DTexture)
			{
				glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				if(IsNewApi())
					glSamplerParameteri(m_Textures[pCommand->m_Slot].m_Sampler2DArray, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			}
			else
			{
				glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(Target, GL_GENERATE_MIPMAP, GL_TRUE);
				if(IsNewApi())
					glSamplerParameteri(m_Textures[pCommand->m_Slot].m_Sampler2DArray, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			}

			glTexParameteri(Target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(Target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(Target, GL_TEXTURE_WRAP_R, GL_MIRRORED_REPEAT);
			if(m_OpenGLTextureLodBIAS != 0)
				glTexParameterf(Target, GL_TEXTURE_LOD_BIAS, ((GLfloat)m_OpenGLTextureLodBIAS / 1000.0f));

			if(IsNewApi())
			{
				glSamplerParameteri(m_Textures[pCommand->m_Slot].m_Sampler2DArray, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glSamplerParameteri(m_Textures[pCommand->m_Slot].m_Sampler2DArray, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glSamplerParameteri(m_Textures[pCommand->m_Slot].m_Sampler2DArray, GL_TEXTURE_WRAP_R, GL_MIRRORED_REPEAT);
				if(m_OpenGLTextureLodBIAS != 0)
					glSamplerParameterf(m_Textures[pCommand->m_Slot].m_Sampler2DArray, GL_TEXTURE_LOD_BIAS, ((GLfloat)m_OpenGLTextureLodBIAS / 1000.0f));

				glBindSampler(0, 0);
			}

			int ImageColorChannels = TexFormatToImageColorChannelCount(pCommand->m_Format);

			uint8_t *p3DImageData = NULL;

			bool IsSingleLayer = (pCommand->m_Flags & (CCommandBuffer::TEXFLAG_TO_2D_ARRAY_TEXTURE_SINGLE_LAYER | CCommandBuffer::TEXFLAG_TO_3D_TEXTURE_SINGLE_LAYER)) != 0;

			if(!IsSingleLayer)
				p3DImageData = (uint8_t *)malloc((size_t)ImageColorChannels * Width * Height);
			int Image3DWidth, Image3DHeight;

			int ConvertWidth = Width;
			int ConvertHeight = Height;

			if(!IsSingleLayer)
			{
				if(ConvertWidth == 0 || (ConvertWidth % 16) != 0 || ConvertHeight == 0 || (ConvertHeight % 16) != 0)
				{
					dbg_msg("gfx", "3D/2D array texture was resized");
					int NewWidth = maximum<int>(HighestBit(ConvertWidth), 16);
					int NewHeight = maximum<int>(HighestBit(ConvertHeight), 16);
					uint8_t *pNewTexData = (uint8_t *)Resize(ConvertWidth, ConvertHeight, NewWidth, NewHeight, pCommand->m_Format, (const uint8_t *)pTexData);

					ConvertWidth = NewWidth;
					ConvertHeight = NewHeight;

					free(pTexData);
					pTexData = pNewTexData;
				}
			}

			if(IsSingleLayer || (Texture2DTo3D(pTexData, ConvertWidth, ConvertHeight, ImageColorChannels, 16, 16, p3DImageData, Image3DWidth, Image3DHeight)))
			{
				if(IsSingleLayer)
				{
					glTexImage3D(Target, 0, StoreOglformat, ConvertWidth, ConvertHeight, 1, 0, Oglformat, GL_UNSIGNED_BYTE, pTexData);
				}
				else
				{
					glTexImage3D(Target, 0, StoreOglformat, Image3DWidth, Image3DHeight, 256, 0, Oglformat, GL_UNSIGNED_BYTE, p3DImageData);
				}

				/*if(StoreOglformat == GL_R8)
				{
					//Bind the texture 2D.
					GLint swizzleMask[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
					glTexParameteriv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
				}*/
			}

			if(!IsSingleLayer)
				free(p3DImageData);
		}
	}

	// This is the initial value for the wrap modes
	m_Textures[pCommand->m_Slot].m_LastWrapMode = CCommandBuffer::WRAP_REPEAT;

	// calculate memory usage
	m_Textures[pCommand->m_Slot].m_MemSize = Width * Height * pCommand->m_PixelSize;
	while(Width > 2 && Height > 2)
	{
		Width >>= 1;
		Height >>= 1;
		m_Textures[pCommand->m_Slot].m_MemSize += Width * Height * pCommand->m_PixelSize;
	}
	m_pTextureMemoryUsage->store(m_pTextureMemoryUsage->load(std::memory_order_relaxed) + m_Textures[pCommand->m_Slot].m_MemSize, std::memory_order_relaxed);

	free(pTexData);
}

void CCommandProcessorFragment_OpenGL::Cmd_Clear(const CCommandBuffer::SCommand_Clear *pCommand)
{
	glClearColor(pCommand->m_Color.r, pCommand->m_Color.g, pCommand->m_Color.b, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void CCommandProcessorFragment_OpenGL::Cmd_Render(const CCommandBuffer::SCommand_Render *pCommand)
{
	SetState(pCommand->m_State);

	glVertexPointer(2, GL_FLOAT, sizeof(CCommandBuffer::SVertex), (char *)pCommand->m_pVertices);
	glTexCoordPointer(2, GL_FLOAT, sizeof(CCommandBuffer::SVertex), (char *)pCommand->m_pVertices + sizeof(float) * 2);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(CCommandBuffer::SVertex), (char *)pCommand->m_pVertices + sizeof(float) * 4);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	switch(pCommand->m_PrimType)
	{
	case CCommandBuffer::PRIMTYPE_QUADS:
		glDrawArrays(GL_QUADS, 0, pCommand->m_PrimCount * 4);
		break;
	case CCommandBuffer::PRIMTYPE_LINES:
		glDrawArrays(GL_LINES, 0, pCommand->m_PrimCount * 2);
		break;
	case CCommandBuffer::PRIMTYPE_TRIANGLES:
		glDrawArrays(GL_TRIANGLES, 0, pCommand->m_PrimCount * 3);
		break;
	default:
		dbg_msg("render", "unknown primtype %d\n", pCommand->m_PrimType);
	};
}

void CCommandProcessorFragment_OpenGL::Cmd_Screenshot(const CCommandBuffer::SCommand_Screenshot *pCommand)
{
	// fetch image data
	GLint aViewport[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_VIEWPORT, aViewport);

	int w = aViewport[2];
	int h = aViewport[3];

	// we allocate one more row to use when we are flipping the texture
	unsigned char *pPixelData = (unsigned char *)malloc((size_t)w * (h + 1) * 3);
	unsigned char *pTempRow = pPixelData + w * h * 3;

	// fetch the pixels
	GLint Alignment;
	glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pPixelData);
	glPixelStorei(GL_PACK_ALIGNMENT, Alignment);

	// flip the pixel because opengl works from bottom left corner
	for(int y = 0; y < h / 2; y++)
	{
		mem_copy(pTempRow, pPixelData + y * w * 3, w * 3);
		mem_copy(pPixelData + y * w * 3, pPixelData + (h - y - 1) * w * 3, w * 3);
		mem_copy(pPixelData + (h - y - 1) * w * 3, pTempRow, w * 3);
	}

	// fill in the information
	pCommand->m_pImage->m_Width = w;
	pCommand->m_pImage->m_Height = h;
	pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGB;
	pCommand->m_pImage->m_pData = pPixelData;
}

CCommandProcessorFragment_OpenGL::CCommandProcessorFragment_OpenGL()
{
	m_Textures.resize(CCommandBuffer::MAX_TEXTURES);
	m_HasShaders = false;
}

bool CCommandProcessorFragment_OpenGL::RunCommand(const CCommandBuffer::SCommand *pBaseCommand)
{
	switch(pBaseCommand->m_Cmd)
	{
	case CCommandProcessorFragment_OpenGL::CMD_INIT:
		Cmd_Init(static_cast<const SCommand_Init *>(pBaseCommand));
		break;
	case CCommandProcessorFragment_OpenGL::CMD_SHUTDOWN:
		Cmd_Shutdown(static_cast<const SCommand_Shutdown *>(pBaseCommand));
		break;
	case CCommandBuffer::CMD_TEXTURE_CREATE:
		Cmd_Texture_Create(static_cast<const CCommandBuffer::SCommand_Texture_Create *>(pBaseCommand));
		break;
	case CCommandBuffer::CMD_TEXTURE_DESTROY:
		Cmd_Texture_Destroy(static_cast<const CCommandBuffer::SCommand_Texture_Destroy *>(pBaseCommand));
		break;
	case CCommandBuffer::CMD_TEXTURE_UPDATE:
		Cmd_Texture_Update(static_cast<const CCommandBuffer::SCommand_Texture_Update *>(pBaseCommand));
		break;
	case CCommandBuffer::CMD_CLEAR:
		Cmd_Clear(static_cast<const CCommandBuffer::SCommand_Clear *>(pBaseCommand));
		break;
	case CCommandBuffer::CMD_RENDER:
		Cmd_Render(static_cast<const CCommandBuffer::SCommand_Render *>(pBaseCommand));
		break;
	case CCommandBuffer::CMD_RENDER_TEX3D:
		Cmd_RenderTex3D(static_cast<const CCommandBuffer::SCommand_RenderTex3D *>(pBaseCommand));
		break;
	case CCommandBuffer::CMD_SCREENSHOT:
		Cmd_Screenshot(static_cast<const CCommandBuffer::SCommand_Screenshot *>(pBaseCommand));
		break;

	case CCommandBuffer::CMD_CREATE_BUFFER_OBJECT: Cmd_CreateBufferObject(static_cast<const CCommandBuffer::SCommand_CreateBufferObject *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_UPDATE_BUFFER_OBJECT: Cmd_UpdateBufferObject(static_cast<const CCommandBuffer::SCommand_UpdateBufferObject *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RECREATE_BUFFER_OBJECT: Cmd_RecreateBufferObject(static_cast<const CCommandBuffer::SCommand_RecreateBufferObject *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_COPY_BUFFER_OBJECT: Cmd_CopyBufferObject(static_cast<const CCommandBuffer::SCommand_CopyBufferObject *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_DELETE_BUFFER_OBJECT: Cmd_DeleteBufferObject(static_cast<const CCommandBuffer::SCommand_DeleteBufferObject *>(pBaseCommand)); break;

	case CCommandBuffer::CMD_CREATE_BUFFER_CONTAINER: Cmd_CreateBufferContainer(static_cast<const CCommandBuffer::SCommand_CreateBufferContainer *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_UPDATE_BUFFER_CONTAINER: Cmd_UpdateBufferContainer(static_cast<const CCommandBuffer::SCommand_UpdateBufferContainer *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_DELETE_BUFFER_CONTAINER: Cmd_DeleteBufferContainer(static_cast<const CCommandBuffer::SCommand_DeleteBufferContainer *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_INDICES_REQUIRED_NUM_NOTIFY: Cmd_IndicesRequiredNumNotify(static_cast<const CCommandBuffer::SCommand_IndicesRequiredNumNotify *>(pBaseCommand)); break;

	case CCommandBuffer::CMD_RENDER_TILE_LAYER: Cmd_RenderTileLayer(static_cast<const CCommandBuffer::SCommand_RenderTileLayer *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_BORDER_TILE: Cmd_RenderBorderTile(static_cast<const CCommandBuffer::SCommand_RenderBorderTile *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_BORDER_TILE_LINE: Cmd_RenderBorderTileLine(static_cast<const CCommandBuffer::SCommand_RenderBorderTileLine *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_QUAD_LAYER: Cmd_RenderQuadLayer(static_cast<const CCommandBuffer::SCommand_RenderQuadLayer *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_TEXT: Cmd_RenderText(static_cast<const CCommandBuffer::SCommand_RenderText *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_TEXT_STREAM: Cmd_RenderTextStream(static_cast<const CCommandBuffer::SCommand_RenderTextStream *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER: Cmd_RenderQuadContainer(static_cast<const CCommandBuffer::SCommand_RenderQuadContainer *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER_EX: Cmd_RenderQuadContainerEx(static_cast<const CCommandBuffer::SCommand_RenderQuadContainerEx *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_QUAD_CONTAINER_SPRITE_MULTIPLE: Cmd_RenderQuadContainerAsSpriteMultiple(static_cast<const CCommandBuffer::SCommand_RenderQuadContainerAsSpriteMultiple *>(pBaseCommand)); break;
	default: return false;
	}

	return true;
}

// ------------ CCommandProcessorFragment_OpenGL2

void CCommandProcessorFragment_OpenGL2::UseProgram(CGLSLTWProgram *pProgram)
{
	pProgram->UseProgram();
}

bool CCommandProcessorFragment_OpenGL2::IsAndUpdateTextureSlotBound(int IDX, int Slot, bool Is2DArray)
{
	if(m_TextureSlotBoundToUnit[IDX].m_TextureSlot == Slot && m_TextureSlotBoundToUnit[IDX].m_Is2DArray == Is2DArray)
		return true;
	else
	{
		//the texture slot uses this index now
		m_TextureSlotBoundToUnit[IDX].m_TextureSlot = Slot;
		m_TextureSlotBoundToUnit[IDX].m_Is2DArray = Is2DArray;
		return false;
	}
}

void CCommandProcessorFragment_OpenGL2::SetState(const CCommandBuffer::SState &State, CGLSLTWProgram *pProgram, bool Use2DArrayTextures)
{
	if(m_LastBlendMode == CCommandBuffer::BLEND_NONE)
	{
		m_LastBlendMode = CCommandBuffer::BLEND_ALPHA;
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	if(State.m_BlendMode != m_LastBlendMode && State.m_BlendMode != CCommandBuffer::BLEND_NONE)
	{
		// blend
		switch(State.m_BlendMode)
		{
		case CCommandBuffer::BLEND_NONE:
			// We don't really need this anymore
			//glDisable(GL_BLEND);
			break;
		case CCommandBuffer::BLEND_ALPHA:
			//glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case CCommandBuffer::BLEND_ADDITIVE:
			//glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			break;
		default:
			dbg_msg("render", "unknown blendmode %d\n", State.m_BlendMode);
		};

		m_LastBlendMode = State.m_BlendMode;
	}

	// clip
	if(State.m_ClipEnable)
	{
		glScissor(State.m_ClipX, State.m_ClipY, State.m_ClipW, State.m_ClipH);
		glEnable(GL_SCISSOR_TEST);
		m_LastClipEnable = true;
	}
	else if(m_LastClipEnable)
	{
		// Don't disable it always
		glDisable(GL_SCISSOR_TEST);
		m_LastClipEnable = false;
	}

	if(!IsNewApi())
	{
		glDisable(GL_TEXTURE_2D);
		if(!m_HasShaders)
		{
			if(m_Has3DTextures)
				glDisable(GL_TEXTURE_3D);
			if(m_Has2DArrayTextures)
			{
				glDisable(m_2DArrayTarget);
			}
		}
	}

	// texture
	if(IsTexturedState(State))
	{
		int Slot = 0;
		if(m_UseMultipleTextureUnits)
		{
			Slot = State.m_Texture % m_MaxTextureUnits;
			if(!IsAndUpdateTextureSlotBound(Slot, State.m_Texture, Use2DArrayTextures))
			{
				glActiveTexture(GL_TEXTURE0 + Slot);
				if(!Use2DArrayTextures)
				{
					glBindTexture(GL_TEXTURE_2D, m_Textures[State.m_Texture].m_Tex);
					if(IsNewApi())
						glBindSampler(Slot, m_Textures[State.m_Texture].m_Sampler);
				}
				else
				{
					glBindTexture(GL_TEXTURE_2D_ARRAY, m_Textures[State.m_Texture].m_Tex2DArray);
					if(IsNewApi())
						glBindSampler(Slot, m_Textures[State.m_Texture].m_Sampler2DArray);
				}
			}
		}
		else
		{
			Slot = 0;
			if(!Use2DArrayTextures)
			{
				if(!IsNewApi() && !m_HasShaders)
					glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, m_Textures[State.m_Texture].m_Tex);
				if(IsNewApi())
					glBindSampler(Slot, m_Textures[State.m_Texture].m_Sampler);
			}
			else
			{
				if(!m_Has2DArrayTextures)
				{
					if(!IsNewApi() && !m_HasShaders)
						glEnable(GL_TEXTURE_3D);
					glBindTexture(GL_TEXTURE_3D, m_Textures[State.m_Texture].m_Tex2DArray);
					if(IsNewApi())
						glBindSampler(Slot, m_Textures[State.m_Texture].m_Sampler2DArray);
				}
				else
				{
					if(!IsNewApi() && !m_HasShaders)
						glEnable(m_2DArrayTarget);
					glBindTexture(m_2DArrayTarget, m_Textures[State.m_Texture].m_Tex2DArray);
					if(IsNewApi())
						glBindSampler(Slot, m_Textures[State.m_Texture].m_Sampler2DArray);
				}
			}
		}

		if(pProgram->m_LastTextureSampler != Slot)
		{
			pProgram->SetUniform(pProgram->m_LocTextureSampler, Slot);
			pProgram->m_LastTextureSampler = Slot;
		}

		if(m_Textures[State.m_Texture].m_LastWrapMode != State.m_WrapMode && !Use2DArrayTextures)
		{
			switch(State.m_WrapMode)
			{
			case CCommandBuffer::WRAP_REPEAT:
				if(IsNewApi())
				{
					glSamplerParameteri(m_Textures[State.m_Texture].m_Sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
					glSamplerParameteri(m_Textures[State.m_Texture].m_Sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
				}
				break;
			case CCommandBuffer::WRAP_CLAMP:
				if(IsNewApi())
				{
					glSamplerParameteri(m_Textures[State.m_Texture].m_Sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glSamplerParameteri(m_Textures[State.m_Texture].m_Sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				}
				break;
			default:
				dbg_msg("render", "unknown wrapmode %d\n", State.m_WrapMode);
			};
			m_Textures[State.m_Texture].m_LastWrapMode = State.m_WrapMode;
		}
	}

	if(pProgram->m_LastScreen[0] != State.m_ScreenTL.x || pProgram->m_LastScreen[1] != State.m_ScreenTL.y || pProgram->m_LastScreen[2] != State.m_ScreenBR.x || pProgram->m_LastScreen[3] != State.m_ScreenBR.y)
	{
		pProgram->m_LastScreen[0] = State.m_ScreenTL.x;
		pProgram->m_LastScreen[1] = State.m_ScreenTL.y;
		pProgram->m_LastScreen[2] = State.m_ScreenBR.x;
		pProgram->m_LastScreen[3] = State.m_ScreenBR.y;
		// screen mapping
		// orthographic projection matrix
		// the z coordinate is the same for every vertex, so just ignore the z coordinate and set it in the shaders
		float m[2 * 4] = {
			2.f / (State.m_ScreenBR.x - State.m_ScreenTL.x), 0, 0, -((State.m_ScreenBR.x + State.m_ScreenTL.x) / (State.m_ScreenBR.x - State.m_ScreenTL.x)),
			0, (2.f / (State.m_ScreenTL.y - State.m_ScreenBR.y)), 0, -((State.m_ScreenTL.y + State.m_ScreenBR.y) / (State.m_ScreenTL.y - State.m_ScreenBR.y)),
			//0, 0, -(2.f/(9.f)), -((11.f)/(9.f)),
			//0, 0, 0, 1.0f
		};

		// transpose bcs of column-major order of opengl
		glUniformMatrix4x2fv(pProgram->m_LocPos, 1, true, (float *)&m);
	}
}

bool CCommandProcessorFragment_OpenGL2::DoAnalyzeStep(size_t StepN, size_t CheckCount, size_t VerticesCount, uint8_t aFakeTexture[], size_t SingleImageSize)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	int Slot = 0;
	if(m_HasShaders)
	{
		CGLSLTWProgram *pProgram = m_pPrimitive3DProgramTextured;
		if(StepN == 1)
			pProgram = m_pTileProgramTextured;
		UseProgram(pProgram);

		pProgram->SetUniform(pProgram->m_LocTextureSampler, Slot);

		if(StepN == 1)
		{
			float aColor[4] = {1.f, 1.f, 1.f, 1.f};
			pProgram->SetUniformVec4(((CGLSLTileProgram *)pProgram)->m_LocColor, 1, aColor);
		}

		float m[2 * 4] = {
			1, 0, 0, 0,
			0, 1, 0, 0};

		// transpose bcs of column-major order of opengl
		glUniformMatrix4x2fv(pProgram->m_LocPos, 1, true, (float *)&m);
	}
	else
	{
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(-1, 1, -1, 1, -10.0f, 10.f);
	}

	GLuint BufferID = 0;
	if(StepN == 1 && m_HasShaders)
	{
		glGenBuffers(1, &BufferID);
		glBindBuffer(GL_ARRAY_BUFFER, BufferID);
		glBufferData(GL_ARRAY_BUFFER, VerticesCount * sizeof((m_aStreamVertices[0])), m_aStreamVertices, GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, false, sizeof((m_aStreamVertices[0])), 0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, false, sizeof((m_aStreamVertices[0])), (GLvoid *)(sizeof(vec4) + sizeof(vec2)));
	}
	else
	{
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);

		glVertexPointer(2, GL_FLOAT, sizeof(m_aStreamVertices[0]), m_aStreamVertices);
		glColorPointer(4, GL_FLOAT, sizeof(m_aStreamVertices[0]), (uint8_t *)m_aStreamVertices + (ptrdiff_t)(sizeof(vec2)));
		glTexCoordPointer(3, GL_FLOAT, sizeof(m_aStreamVertices[0]), (uint8_t *)m_aStreamVertices + (ptrdiff_t)(sizeof(vec2) + sizeof(vec4)));
	}

	glDrawArrays(GL_QUADS, 0, VerticesCount);

	if(StepN == 1 && m_HasShaders)
	{
		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDeleteBuffers(1, &BufferID);
	}
	else
	{
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	if(m_HasShaders)
	{
		glUseProgram(0);
	}

	glFinish();

	GLint aViewport[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_VIEWPORT, aViewport);

	int w = aViewport[2];
	int h = aViewport[3];

	size_t PixelDataSize = (size_t)w * h * 3;
	if(PixelDataSize == 0)
		return false;
	uint8_t *pPixelData = (uint8_t *)malloc(PixelDataSize);

	// fetch the pixels
	GLint Alignment;
	glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pPixelData);
	glPixelStorei(GL_PACK_ALIGNMENT, Alignment);

	// now analyse the image data
	bool CheckFailed = false;
	int WidthTile = w / 16;
	int HeightTile = h / 16;
	int StartX = WidthTile / 2;
	int StartY = HeightTile / 2;
	for(size_t d = 0; d < CheckCount; ++d)
	{
		int CurX = (int)d % 16;
		int CurY = (int)d / 16;

		int CheckX = StartX + CurX * WidthTile;
		int CheckY = StartY + CurY * HeightTile;

		ptrdiff_t OffsetPixelData = (CheckY * (w * 3)) + (CheckX * 3);
		ptrdiff_t OffsetFakeTexture = SingleImageSize * d;
		OffsetPixelData = clamp<ptrdiff_t>(OffsetPixelData, 0, (ptrdiff_t)PixelDataSize);
		OffsetFakeTexture = clamp<ptrdiff_t>(OffsetFakeTexture, 0, (ptrdiff_t)(SingleImageSize * CheckCount));
		uint8_t *pPixel = pPixelData + OffsetPixelData;
		uint8_t *pPixelTex = aFakeTexture + OffsetFakeTexture;
		for(size_t i = 0; i < 3; ++i)
		{
			if((pPixel[i] < pPixelTex[i] - 25) || (pPixel[i] > pPixelTex[i] + 25))
			{
				CheckFailed = true;
				break;
			}
		}
	}

	free(pPixelData);
	return !CheckFailed;
}

bool CCommandProcessorFragment_OpenGL2::IsTileMapAnalysisSucceeded()
{
	glClearColor(0, 0, 0, 1);

	// create fake texture 1024x1024
	const size_t ImageWidth = 1024;
	const size_t ImageHeight = 1024;
	uint8_t *pFakeTexture = (uint8_t *)malloc(sizeof(uint8_t) * ImageWidth * ImageHeight * 4);
	// fill by colors stepping by 50 => (255 / 50 ~ 5) => 5 times 3(color channels) = 5 ^ 3 = 125 possibilities to check
	size_t CheckCount = 5 * 5 * 5;
	// always fill 4 pixels of the texture, so the sampling is accurate
	int aCurColor[4] = {25, 25, 25, 255};
	const size_t SingleImageWidth = 64;
	const size_t SingleImageHeight = 64;
	size_t SingleImageSize = SingleImageWidth * SingleImageHeight * 4;
	for(size_t d = 0; d < CheckCount; ++d)
	{
		uint8_t *pCurFakeTexture = pFakeTexture + (ptrdiff_t)(SingleImageSize * d);

		uint8_t aCurColorUint8[SingleImageWidth * SingleImageHeight * 4];
		for(size_t y = 0; y < SingleImageHeight; ++y)
		{
			for(size_t x = 0; x < SingleImageWidth; ++x)
			{
				for(size_t i = 0; i < 4; ++i)
				{
					aCurColorUint8[(y * SingleImageWidth * 4) + (x * 4) + i] = (uint8_t)aCurColor[i];
				}
			}
		}
		mem_copy(pCurFakeTexture, aCurColorUint8, sizeof(aCurColorUint8));

		aCurColor[2] += 50;
		if(aCurColor[2] > 225)
		{
			aCurColor[2] -= 250;
			aCurColor[1] += 50;
		}
		if(aCurColor[1] > 225)
		{
			aCurColor[1] -= 250;
			aCurColor[0] += 50;
		}
		if(aCurColor[0] > 225)
		{
			break;
		}
	}

	// upload the texture
	GLuint FakeTexture;
	glGenTextures(1, &FakeTexture);

	GLenum Target = GL_TEXTURE_3D;
	if(m_Has2DArrayTextures)
	{
		Target = m_2DArrayTarget;
	}

	glBindTexture(Target, FakeTexture);
	glTexParameteri(Target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if(!m_Has2DArrayTextures)
	{
		glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	else
	{
		glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(Target, GL_GENERATE_MIPMAP, GL_TRUE);
	}

	glTexParameteri(Target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(Target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(Target, GL_TEXTURE_WRAP_R, GL_MIRRORED_REPEAT);

	glTexImage3D(Target, 0, GL_RGBA, ImageWidth / 16, ImageHeight / 16, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, pFakeTexture);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_SCISSOR_TEST);

	if(!m_HasShaders)
	{
		glDisable(GL_TEXTURE_2D);
		if(m_Has3DTextures)
			glDisable(GL_TEXTURE_3D);
		if(m_Has2DArrayTextures)
		{
			glDisable(m_2DArrayTarget);
		}

		if(!m_Has2DArrayTextures)
		{
			glEnable(GL_TEXTURE_3D);
			glBindTexture(GL_TEXTURE_3D, FakeTexture);
		}
		else
		{
			glEnable(m_2DArrayTarget);
			glBindTexture(m_2DArrayTarget, FakeTexture);
		}
	}

	static_assert(sizeof(m_aStreamVertices) / sizeof(m_aStreamVertices[0]) >= 256 * 4, "Keep the number of stream vertices >= 256 * 4.");

	size_t VertexCount = 0;
	for(size_t i = 0; i < CheckCount; ++i)
	{
		float XPos = (float)(i % 16);
		float YPos = (float)(i / 16);

		GL_SVertexTex3D *pVertex = &m_aStreamVertices[VertexCount++];
		GL_SVertexTex3D *pVertexBefore = pVertex;
		pVertex->m_Pos.x = XPos / 16.f;
		pVertex->m_Pos.y = YPos / 16.f;
		pVertex->m_Color.r = 1;
		pVertex->m_Color.g = 1;
		pVertex->m_Color.b = 1;
		pVertex->m_Color.a = 1;
		pVertex->m_Tex.u = 0;
		pVertex->m_Tex.v = 0;

		pVertex = &m_aStreamVertices[VertexCount++];
		pVertex->m_Pos.x = XPos / 16.f + 1.f / 16.f;
		pVertex->m_Pos.y = YPos / 16.f;
		pVertex->m_Color.r = 1;
		pVertex->m_Color.g = 1;
		pVertex->m_Color.b = 1;
		pVertex->m_Color.a = 1;
		pVertex->m_Tex.u = 1;
		pVertex->m_Tex.v = 0;

		pVertex = &m_aStreamVertices[VertexCount++];
		pVertex->m_Pos.x = XPos / 16.f + 1.f / 16.f;
		pVertex->m_Pos.y = YPos / 16.f + 1.f / 16.f;
		pVertex->m_Color.r = 1;
		pVertex->m_Color.g = 1;
		pVertex->m_Color.b = 1;
		pVertex->m_Color.a = 1;
		pVertex->m_Tex.u = 1;
		pVertex->m_Tex.v = 1;

		pVertex = &m_aStreamVertices[VertexCount++];
		pVertex->m_Pos.x = XPos / 16.f;
		pVertex->m_Pos.y = YPos / 16.f + 1.f / 16.f;
		pVertex->m_Color.r = 1;
		pVertex->m_Color.g = 1;
		pVertex->m_Color.b = 1;
		pVertex->m_Color.a = 1;
		pVertex->m_Tex.u = 0;
		pVertex->m_Tex.v = 1;

		for(size_t n = 0; n < 4; ++n)
		{
			pVertexBefore[n].m_Pos.x *= 2;
			pVertexBefore[n].m_Pos.x -= 1;
			pVertexBefore[n].m_Pos.y *= 2;
			pVertexBefore[n].m_Pos.y -= 1;
			if(m_Has2DArrayTextures)
			{
				pVertexBefore[n].m_Tex.w = i;
			}
			else
			{
				pVertexBefore[n].m_Tex.w = (i + 0.5f) / 256.f;
			}
		}
	}

	//everything build up, now do the analyze steps
	bool NoError = DoAnalyzeStep(0, CheckCount, VertexCount, pFakeTexture, SingleImageSize);
	if(NoError && m_HasShaders)
		NoError &= DoAnalyzeStep(1, CheckCount, VertexCount, pFakeTexture, SingleImageSize);

	glDeleteTextures(1, &FakeTexture);
	free(pFakeTexture);

	return NoError;
}

void CCommandProcessorFragment_OpenGL2::Cmd_Init(const SCommand_Init *pCommand)
{
	CCommandProcessorFragment_OpenGL::Cmd_Init(pCommand);

	m_OpenGLTextureLodBIAS = g_Config.m_GfxOpenGLTextureLODBIAS;

	m_HasShaders = pCommand->m_pCapabilities->m_ShaderSupport;

	bool HasAllFunc = true;
	if(m_HasShaders)
	{
		HasAllFunc &= (glUniformMatrix4x2fv != NULL) && (glGenBuffers != NULL);
		HasAllFunc &= (glBindBuffer != NULL) && (glBufferData != NULL);
		HasAllFunc &= (glEnableVertexAttribArray != NULL) && (glVertexAttribPointer != NULL);
		HasAllFunc &= (glDisableVertexAttribArray != NULL) && (glDeleteBuffers != NULL);
		HasAllFunc &= (glUseProgram != NULL) && (glTexImage3D != NULL);
		HasAllFunc &= (glBindAttribLocation != NULL) && (glTexImage3D != NULL);
		HasAllFunc &= (glBufferSubData != NULL) && (glGetUniformLocation != NULL);
		HasAllFunc &= (glUniform1i != NULL) && (glUniform1f != NULL);
		HasAllFunc &= (glUniform1ui != NULL) && (glUniform1i != NULL);
		HasAllFunc &= (glUniform1fv != NULL) && (glUniform2fv != NULL);
		HasAllFunc &= (glUniform4fv != NULL) && (glGetAttachedShaders != NULL);
		HasAllFunc &= (glGetProgramInfoLog != NULL) && (glGetProgramiv != NULL);
		HasAllFunc &= (glLinkProgram != NULL) && (glDetachShader != NULL);
		HasAllFunc &= (glAttachShader != NULL) && (glDeleteProgram != NULL);
		HasAllFunc &= (glCreateProgram != NULL) && (glShaderSource != NULL);
		HasAllFunc &= (glCompileShader != NULL) && (glGetShaderiv != NULL);
		HasAllFunc &= (glGetShaderInfoLog != NULL) && (glDeleteShader != NULL);
		HasAllFunc &= (glCreateShader != NULL);
	}

	bool AnalysisCorrect = true;
	if(HasAllFunc)
	{
		if(m_HasShaders)
		{
			m_pTileProgram = new CGLSLTileProgram;
			m_pTileProgramTextured = new CGLSLTileProgram;
			m_pPrimitive3DProgram = new CGLSLPrimitiveProgram;
			m_pPrimitive3DProgramTextured = new CGLSLPrimitiveProgram;

			CGLSLCompiler ShaderCompiler(g_Config.m_GfxOpenGLMajor, g_Config.m_GfxOpenGLMinor, g_Config.m_GfxOpenGLPatch);
			ShaderCompiler.SetHasTextureArray(pCommand->m_pCapabilities->m_2DArrayTextures);

			if(pCommand->m_pCapabilities->m_2DArrayTextures)
				ShaderCompiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_2D_ARRAY);
			else
				ShaderCompiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_3D);
			{
				CGLSL PrimitiveVertexShader;
				CGLSL PrimitiveFragmentShader;
				PrimitiveVertexShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/pipeline.vert", GL_VERTEX_SHADER);
				PrimitiveFragmentShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/pipeline.frag", GL_FRAGMENT_SHADER);

				m_pPrimitive3DProgram->CreateProgram();
				m_pPrimitive3DProgram->AddShader(&PrimitiveVertexShader);
				m_pPrimitive3DProgram->AddShader(&PrimitiveFragmentShader);
				m_pPrimitive3DProgram->LinkProgram();

				UseProgram(m_pPrimitive3DProgram);

				m_pPrimitive3DProgram->m_LocPos = m_pPrimitive3DProgram->GetUniformLoc("gPos");
			}

			if(pCommand->m_pCapabilities->m_2DArrayTextures)
				ShaderCompiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_2D_ARRAY);
			else
				ShaderCompiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_3D);
			{
				CGLSL PrimitiveVertexShader;
				CGLSL PrimitiveFragmentShader;
				ShaderCompiler.AddDefine("TW_TEXTURED", "");
				if(!pCommand->m_pCapabilities->m_2DArrayTextures)
					ShaderCompiler.AddDefine("TW_3D_TEXTURED", "");
				PrimitiveVertexShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/pipeline.vert", GL_VERTEX_SHADER);
				PrimitiveFragmentShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/pipeline.frag", GL_FRAGMENT_SHADER);
				ShaderCompiler.ClearDefines();

				m_pPrimitive3DProgramTextured->CreateProgram();
				m_pPrimitive3DProgramTextured->AddShader(&PrimitiveVertexShader);
				m_pPrimitive3DProgramTextured->AddShader(&PrimitiveFragmentShader);
				m_pPrimitive3DProgramTextured->LinkProgram();

				UseProgram(m_pPrimitive3DProgramTextured);

				m_pPrimitive3DProgramTextured->m_LocPos = m_pPrimitive3DProgramTextured->GetUniformLoc("gPos");
				m_pPrimitive3DProgramTextured->m_LocTextureSampler = m_pPrimitive3DProgramTextured->GetUniformLoc("gTextureSampler");
			}
			if(pCommand->m_pCapabilities->m_2DArrayTextures)
				ShaderCompiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_2D_ARRAY);
			else
				ShaderCompiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_3D);
			{
				CGLSL VertexShader;
				CGLSL FragmentShader;
				VertexShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/tile.vert", GL_VERTEX_SHADER);
				FragmentShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/tile.frag", GL_FRAGMENT_SHADER);

				m_pTileProgram->CreateProgram();
				m_pTileProgram->AddShader(&VertexShader);
				m_pTileProgram->AddShader(&FragmentShader);

				glBindAttribLocation(m_pTileProgram->GetProgramID(), 0, "inVertex");

				m_pTileProgram->LinkProgram();

				UseProgram(m_pTileProgram);

				m_pTileProgram->m_LocPos = m_pTileProgram->GetUniformLoc("gPos");
				m_pTileProgram->m_LocColor = m_pTileProgram->GetUniformLoc("gVertColor");
			}
			if(pCommand->m_pCapabilities->m_2DArrayTextures)
				ShaderCompiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_2D_ARRAY);
			else
				ShaderCompiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_3D);
			{
				CGLSL VertexShader;
				CGLSL FragmentShader;
				ShaderCompiler.AddDefine("TW_TILE_TEXTURED", "");
				if(!pCommand->m_pCapabilities->m_2DArrayTextures)
					ShaderCompiler.AddDefine("TW_TILE_3D_TEXTURED", "");
				VertexShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/tile.vert", GL_VERTEX_SHADER);
				FragmentShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/tile.frag", GL_FRAGMENT_SHADER);
				ShaderCompiler.ClearDefines();

				m_pTileProgramTextured->CreateProgram();
				m_pTileProgramTextured->AddShader(&VertexShader);
				m_pTileProgramTextured->AddShader(&FragmentShader);

				glBindAttribLocation(m_pTileProgram->GetProgramID(), 0, "inVertex");
				glBindAttribLocation(m_pTileProgram->GetProgramID(), 1, "inVertexTexCoord");

				m_pTileProgramTextured->LinkProgram();

				UseProgram(m_pTileProgramTextured);

				m_pTileProgramTextured->m_LocPos = m_pTileProgramTextured->GetUniformLoc("gPos");
				m_pTileProgramTextured->m_LocTextureSampler = m_pTileProgramTextured->GetUniformLoc("gTextureSampler");
				m_pTileProgramTextured->m_LocColor = m_pTileProgramTextured->GetUniformLoc("gVertColor");
			}

			glUseProgram(0);
		}

		if(g_Config.m_Gfx3DTextureAnalysisDone == 0)
		{
			AnalysisCorrect = IsTileMapAnalysisSucceeded();
			if(AnalysisCorrect)
			{
				g_Config.m_Gfx3DTextureAnalysisDone = 1;
			}
		}
	}

	if(!AnalysisCorrect || !HasAllFunc)
	{
		// downgrade to opengl 1.5
		*pCommand->m_pInitError = -2;
		pCommand->m_pCapabilities->m_ContextMajor = 1;
		pCommand->m_pCapabilities->m_ContextMinor = 5;
		pCommand->m_pCapabilities->m_ContextPatch = 0;
	}
}

void CCommandProcessorFragment_OpenGL2::Cmd_RenderTex3D(const CCommandBuffer::SCommand_RenderTex3D *pCommand)
{
	if(m_HasShaders)
	{
		CGLSLPrimitiveProgram *pProgram = NULL;
		if(IsTexturedState(pCommand->m_State))
		{
			pProgram = m_pPrimitive3DProgramTextured;
		}
		else
			pProgram = m_pPrimitive3DProgram;

		UseProgram(pProgram);

		SetState(pCommand->m_State, pProgram, true);
	}
	else
	{
		CCommandProcessorFragment_OpenGL::SetState(pCommand->m_State, true);
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(2, GL_FLOAT, sizeof(pCommand->m_pVertices[0]), pCommand->m_pVertices);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(pCommand->m_pVertices[0]), (uint8_t *)pCommand->m_pVertices + (ptrdiff_t)(sizeof(vec2)));
	glTexCoordPointer(3, GL_FLOAT, sizeof(pCommand->m_pVertices[0]), (uint8_t *)pCommand->m_pVertices + (ptrdiff_t)(sizeof(vec2) + sizeof(unsigned char) * 4));

	switch(pCommand->m_PrimType)
	{
	case CCommandBuffer::PRIMTYPE_QUADS:
		glDrawArrays(GL_QUADS, 0, pCommand->m_PrimCount * 4);
		break;
	case CCommandBuffer::PRIMTYPE_TRIANGLES:
		glDrawArrays(GL_TRIANGLES, 0, pCommand->m_PrimCount * 3);
		break;
	default:
		dbg_msg("render", "unknown primtype %d\n", pCommand->m_PrimType);
	};

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	if(m_HasShaders)
	{
		glUseProgram(0);
	}
}

void CCommandProcessorFragment_OpenGL2::Cmd_CreateBufferObject(const CCommandBuffer::SCommand_CreateBufferObject *pCommand)
{
	void *pUploadData = pCommand->m_pUploadData;
	int Index = pCommand->m_BufferIndex;
	//create necessary space
	if((size_t)Index >= m_BufferObjectIndices.size())
	{
		for(int i = m_BufferObjectIndices.size(); i < Index + 1; ++i)
		{
			m_BufferObjectIndices.push_back(SBufferObject(0));
		}
	}

	GLuint VertBufferID = 0;

	if(m_HasShaders)
	{
		glGenBuffers(1, &VertBufferID);
		glBindBuffer(GL_COPY_WRITE_BUFFER, VertBufferID);
		glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)(pCommand->m_DataSize), pUploadData, GL_STATIC_DRAW);
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
	}

	SBufferObject &BufferObject = m_BufferObjectIndices[Index];
	BufferObject.m_BufferObjectID = VertBufferID;
	BufferObject.m_DataSize = pCommand->m_DataSize;
	BufferObject.m_pData = malloc(pCommand->m_DataSize);
	if(pUploadData)
		mem_copy(BufferObject.m_pData, pUploadData, pCommand->m_DataSize);

	if(pCommand->m_DeletePointer)
		free(pUploadData);
}

void CCommandProcessorFragment_OpenGL2::Cmd_RecreateBufferObject(const CCommandBuffer::SCommand_RecreateBufferObject *pCommand)
{
	void *pUploadData = pCommand->m_pUploadData;
	int Index = pCommand->m_BufferIndex;
	SBufferObject &BufferObject = m_BufferObjectIndices[Index];

	if(m_HasShaders)
	{
		glBindBuffer(GL_COPY_WRITE_BUFFER, BufferObject.m_BufferObjectID);
		glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)(pCommand->m_DataSize), pUploadData, GL_STATIC_DRAW);
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
	}

	BufferObject.m_DataSize = pCommand->m_DataSize;
	if(BufferObject.m_pData)
		free(BufferObject.m_pData);
	BufferObject.m_pData = malloc(pCommand->m_DataSize);
	if(pUploadData)
		mem_copy(BufferObject.m_pData, pUploadData, pCommand->m_DataSize);

	if(pCommand->m_DeletePointer)
		free(pUploadData);
}

void CCommandProcessorFragment_OpenGL2::Cmd_UpdateBufferObject(const CCommandBuffer::SCommand_UpdateBufferObject *pCommand)
{
	void *pUploadData = pCommand->m_pUploadData;
	int Index = pCommand->m_BufferIndex;
	SBufferObject &BufferObject = m_BufferObjectIndices[Index];

	if(m_HasShaders)
	{
		glBindBuffer(GL_COPY_WRITE_BUFFER, BufferObject.m_BufferObjectID);
		glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)(pCommand->m_pOffset), (GLsizeiptr)(pCommand->m_DataSize), pUploadData);
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
	}

	if(pUploadData)
		mem_copy(((uint8_t *)BufferObject.m_pData) + (ptrdiff_t)pCommand->m_pOffset, pUploadData, pCommand->m_DataSize);

	if(pCommand->m_DeletePointer)
		free(pUploadData);
}

void CCommandProcessorFragment_OpenGL2::Cmd_CopyBufferObject(const CCommandBuffer::SCommand_CopyBufferObject *pCommand)
{
	int WriteIndex = pCommand->m_WriteBufferIndex;
	int ReadIndex = pCommand->m_ReadBufferIndex;

	SBufferObject &ReadBufferObject = m_BufferObjectIndices[ReadIndex];
	SBufferObject &WriteBufferObject = m_BufferObjectIndices[WriteIndex];

	mem_copy(((uint8_t *)WriteBufferObject.m_pData) + (ptrdiff_t)pCommand->m_pWriteOffset, ((uint8_t *)ReadBufferObject.m_pData) + (ptrdiff_t)pCommand->m_pReadOffset, pCommand->m_CopySize);

	if(m_HasShaders)
	{
		glBindBuffer(GL_COPY_WRITE_BUFFER, WriteBufferObject.m_BufferObjectID);
		glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)(pCommand->m_pWriteOffset), (GLsizeiptr)(pCommand->m_CopySize), ((uint8_t *)WriteBufferObject.m_pData) + (ptrdiff_t)pCommand->m_pWriteOffset);
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
	}
}

void CCommandProcessorFragment_OpenGL2::Cmd_DeleteBufferObject(const CCommandBuffer::SCommand_DeleteBufferObject *pCommand)
{
	int Index = pCommand->m_BufferIndex;
	SBufferObject &BufferObject = m_BufferObjectIndices[Index];

	if(m_HasShaders)
	{
		glDeleteBuffers(1, &BufferObject.m_BufferObjectID);
	}

	if(BufferObject.m_pData)
	{
		free(BufferObject.m_pData);
		BufferObject.m_pData = NULL;
	}
}

void CCommandProcessorFragment_OpenGL2::Cmd_CreateBufferContainer(const CCommandBuffer::SCommand_CreateBufferContainer *pCommand)
{
	int Index = pCommand->m_BufferContainerIndex;
	//create necessary space
	if((size_t)Index >= m_BufferContainers.size())
	{
		for(int i = m_BufferContainers.size(); i < Index + 1; ++i)
		{
			SBufferContainer Container;
			Container.m_ContainerInfo.m_Stride = 0;
			m_BufferContainers.push_back(Container);
		}
	}

	SBufferContainer &BufferContainer = m_BufferContainers[Index];

	for(int i = 0; i < pCommand->m_AttrCount; ++i)
	{
		SBufferContainerInfo::SAttribute &Attr = pCommand->m_Attributes[i];
		BufferContainer.m_ContainerInfo.m_Attributes.push_back(Attr);
	}

	BufferContainer.m_ContainerInfo.m_Stride = pCommand->m_Stride;
}

void CCommandProcessorFragment_OpenGL2::Cmd_UpdateBufferContainer(const CCommandBuffer::SCommand_UpdateBufferContainer *pCommand)
{
	SBufferContainer &BufferContainer = m_BufferContainers[pCommand->m_BufferContainerIndex];

	BufferContainer.m_ContainerInfo.m_Attributes.clear();

	for(int i = 0; i < pCommand->m_AttrCount; ++i)
	{
		SBufferContainerInfo::SAttribute &Attr = pCommand->m_Attributes[i];
		BufferContainer.m_ContainerInfo.m_Attributes.push_back(Attr);
	}

	BufferContainer.m_ContainerInfo.m_Stride = pCommand->m_Stride;
}

void CCommandProcessorFragment_OpenGL2::Cmd_DeleteBufferContainer(const CCommandBuffer::SCommand_DeleteBufferContainer *pCommand)
{
	SBufferContainer &BufferContainer = m_BufferContainers[pCommand->m_BufferContainerIndex];

	if(pCommand->m_DestroyAllBO)
	{
		for(size_t i = 0; i < BufferContainer.m_ContainerInfo.m_Attributes.size(); ++i)
		{
			int VertBufferID = BufferContainer.m_ContainerInfo.m_Attributes[i].m_VertBufferBindingIndex;
			if(VertBufferID != -1)
			{
				for(auto &Attribute : BufferContainer.m_ContainerInfo.m_Attributes)
				{
					// set all equal ids to zero to not double delete
					if(VertBufferID == Attribute.m_VertBufferBindingIndex)
					{
						Attribute.m_VertBufferBindingIndex = -1;
					}
				}

				if(m_HasShaders)
				{
					glDeleteBuffers(1, &m_BufferObjectIndices[VertBufferID].m_BufferObjectID);
				}

				if(m_BufferObjectIndices[VertBufferID].m_pData)
				{
					free(m_BufferObjectIndices[VertBufferID].m_pData);
					m_BufferObjectIndices[VertBufferID].m_pData = NULL;
				}
			}
		}
	}

	BufferContainer.m_ContainerInfo.m_Attributes.clear();
}

void CCommandProcessorFragment_OpenGL2::Cmd_IndicesRequiredNumNotify(const CCommandBuffer::SCommand_IndicesRequiredNumNotify *pCommand)
{
}

void CCommandProcessorFragment_OpenGL2::RenderBorderTileEmulation(SBufferContainer &BufferContainer, const CCommandBuffer::SState &State, const float *pColor, const char *pBuffOffset, unsigned int DrawNum, const float *pOffset, const float *pDir, int JumpIndex)
{
	if(m_HasShaders)
	{
		CGLSLPrimitiveProgram *pProgram = NULL;
		if(IsTexturedState(State))
		{
			pProgram = m_pPrimitive3DProgramTextured;
		}
		else
			pProgram = m_pPrimitive3DProgram;

		UseProgram(pProgram);

		SetState(State, pProgram, true);
	}
	else
	{
		CCommandProcessorFragment_OpenGL::SetState(State, true);
	}

	bool IsTextured = BufferContainer.m_ContainerInfo.m_Attributes.size() == 2;

	SBufferObject &BufferObject = m_BufferObjectIndices[(size_t)BufferContainer.m_ContainerInfo.m_Attributes[0].m_VertBufferBindingIndex];

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	if(IsTextured)
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(2, GL_FLOAT, sizeof(m_aStreamVertices[0]), m_aStreamVertices);
	glColorPointer(4, GL_FLOAT, sizeof(m_aStreamVertices[0]), (uint8_t *)m_aStreamVertices + (ptrdiff_t)(sizeof(vec2)));
	if(IsTextured)
		glTexCoordPointer(3, GL_FLOAT, sizeof(m_aStreamVertices[0]), (uint8_t *)m_aStreamVertices + (ptrdiff_t)(sizeof(vec2) + sizeof(vec4)));

	size_t VertexCount = 0;
	for(size_t i = 0; i < DrawNum; ++i)
	{
		GLint RealOffset = (GLint)((((size_t)(uintptr_t)(pBuffOffset)) / (6 * sizeof(unsigned int))) * 4);
		size_t SingleVertSize = (sizeof(vec2) + (IsTextured ? sizeof(vec3) : 0));
		size_t CurBufferOffset = (RealOffset)*SingleVertSize;

		for(size_t n = 0; n < 4; ++n)
		{
			int XCount = i - (int(i / JumpIndex) * JumpIndex);
			int YCount = (int(i / JumpIndex));

			ptrdiff_t VertOffset = (ptrdiff_t)(CurBufferOffset + (n * SingleVertSize));
			vec2 *pPos = (vec2 *)((uint8_t *)BufferObject.m_pData + VertOffset);

			GL_SVertexTex3D &Vertex = m_aStreamVertices[VertexCount++];
			mem_copy(&Vertex.m_Pos, pPos, sizeof(vec2));
			mem_copy(&Vertex.m_Color, pColor, sizeof(vec4));
			if(IsTextured)
			{
				vec3 *pTex = (vec3 *)((uint8_t *)BufferObject.m_pData + VertOffset + (ptrdiff_t)sizeof(vec2));
				mem_copy(&Vertex.m_Tex, pTex, sizeof(vec3));
			}

			Vertex.m_Pos.x += pOffset[0] + pDir[0] * XCount;
			Vertex.m_Pos.y += pOffset[1] + pDir[1] * YCount;

			if(VertexCount >= sizeof(m_aStreamVertices) / sizeof(m_aStreamVertices[0]))
			{
				glDrawArrays(GL_QUADS, 0, VertexCount);
				VertexCount = 0;
			}
		}
	}
	if(VertexCount > 0)
		glDrawArrays(GL_QUADS, 0, VertexCount);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	if(IsTextured)
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	if(m_HasShaders)
	{
		glUseProgram(0);
	}
}

void CCommandProcessorFragment_OpenGL2::RenderBorderTileLineEmulation(SBufferContainer &BufferContainer, const CCommandBuffer::SState &State, const float *pColor, const char *pBuffOffset, unsigned int IndexDrawNum, unsigned int DrawNum, const float *pOffset, const float *pDir)
{
	if(m_HasShaders)
	{
		CGLSLPrimitiveProgram *pProgram = NULL;
		if(IsTexturedState(State))
		{
			pProgram = m_pPrimitive3DProgramTextured;
		}
		else
			pProgram = m_pPrimitive3DProgram;

		UseProgram(pProgram);

		SetState(State, pProgram, true);
	}
	else
	{
		CCommandProcessorFragment_OpenGL::SetState(State, true);
	}

	bool IsTextured = BufferContainer.m_ContainerInfo.m_Attributes.size() == 2;

	SBufferObject &BufferObject = m_BufferObjectIndices[(size_t)BufferContainer.m_ContainerInfo.m_Attributes[0].m_VertBufferBindingIndex];

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	if(IsTextured)
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(2, GL_FLOAT, sizeof(m_aStreamVertices[0]), m_aStreamVertices);
	glColorPointer(4, GL_FLOAT, sizeof(m_aStreamVertices[0]), (uint8_t *)m_aStreamVertices + (ptrdiff_t)(sizeof(vec2)));
	if(IsTextured)
		glTexCoordPointer(3, GL_FLOAT, sizeof(m_aStreamVertices[0]), (uint8_t *)m_aStreamVertices + (ptrdiff_t)(sizeof(vec2) + sizeof(vec4)));

	size_t VertexCount = 0;
	for(size_t i = 0; i < DrawNum; ++i)
	{
		GLint RealOffset = (GLint)((((size_t)(uintptr_t)(pBuffOffset)) / (6 * sizeof(unsigned int))) * 4);
		size_t SingleVertSize = (sizeof(vec2) + (IsTextured ? sizeof(vec3) : 0));
		size_t CurBufferOffset = (RealOffset)*SingleVertSize;
		size_t VerticesPerLine = (size_t)IndexDrawNum / 6;

		for(size_t n = 0; n < 4 * (size_t)VerticesPerLine; ++n)
		{
			ptrdiff_t VertOffset = (ptrdiff_t)(CurBufferOffset + (n * SingleVertSize));
			vec2 *pPos = (vec2 *)((uint8_t *)BufferObject.m_pData + VertOffset);

			GL_SVertexTex3D &Vertex = m_aStreamVertices[VertexCount++];
			mem_copy(&Vertex.m_Pos, pPos, sizeof(vec2));
			mem_copy(&Vertex.m_Color, pColor, sizeof(vec4));
			if(IsTextured)
			{
				vec3 *pTex = (vec3 *)((uint8_t *)BufferObject.m_pData + VertOffset + (ptrdiff_t)sizeof(vec2));
				mem_copy(&Vertex.m_Tex, pTex, sizeof(vec3));
			}

			Vertex.m_Pos.x += pOffset[0] + pDir[0] * i;
			Vertex.m_Pos.y += pOffset[1] + pDir[1] * i;

			if(VertexCount >= sizeof(m_aStreamVertices) / sizeof(m_aStreamVertices[0]))
			{
				glDrawArrays(GL_QUADS, 0, VertexCount);
				VertexCount = 0;
			}
		}
	}
	if(VertexCount > 0)
		glDrawArrays(GL_QUADS, 0, VertexCount);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	if(IsTextured)
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	if(m_HasShaders)
	{
		glUseProgram(0);
	}
}

void CCommandProcessorFragment_OpenGL2::Cmd_RenderBorderTile(const CCommandBuffer::SCommand_RenderBorderTile *pCommand)
{
	int Index = pCommand->m_BufferContainerIndex;
	//if space not there return
	if((size_t)Index >= m_BufferContainers.size())
		return;

	SBufferContainer &BufferContainer = m_BufferContainers[Index];

	RenderBorderTileEmulation(BufferContainer, pCommand->m_State, (float *)&pCommand->m_Color, pCommand->m_pIndicesOffset, pCommand->m_DrawNum, pCommand->m_Offset, pCommand->m_Dir, pCommand->m_JumpIndex);
}

void CCommandProcessorFragment_OpenGL2::Cmd_RenderBorderTileLine(const CCommandBuffer::SCommand_RenderBorderTileLine *pCommand)
{
	int Index = pCommand->m_BufferContainerIndex;
	//if space not there return
	if((size_t)Index >= m_BufferContainers.size())
		return;

	SBufferContainer &BufferContainer = m_BufferContainers[Index];

	RenderBorderTileLineEmulation(BufferContainer, pCommand->m_State, (float *)&pCommand->m_Color, pCommand->m_pIndicesOffset, pCommand->m_IndexDrawNum, pCommand->m_DrawNum, pCommand->m_Offset, pCommand->m_Dir);
}

void CCommandProcessorFragment_OpenGL2::Cmd_RenderTileLayer(const CCommandBuffer::SCommand_RenderTileLayer *pCommand)
{
	int Index = pCommand->m_BufferContainerIndex;
	//if space not there return
	if((size_t)Index >= m_BufferContainers.size())
		return;

	SBufferContainer &BufferContainer = m_BufferContainers[Index];

	if(pCommand->m_IndicesDrawNum == 0)
	{
		return; //nothing to draw
	}

	if(m_HasShaders)
	{
		CGLSLTileProgram *pProgram = NULL;
		if(IsTexturedState(pCommand->m_State))
		{
			pProgram = m_pTileProgramTextured;
		}
		else
			pProgram = m_pTileProgram;

		UseProgram(pProgram);

		SetState(pCommand->m_State, pProgram, true);
		pProgram->SetUniformVec4(pProgram->m_LocColor, 1, (float *)&pCommand->m_Color);
	}
	else
	{
		CCommandProcessorFragment_OpenGL::SetState(pCommand->m_State, true);
	}

	bool IsTextured = BufferContainer.m_ContainerInfo.m_Attributes.size() == 2;

	SBufferObject &BufferObject = m_BufferObjectIndices[(size_t)BufferContainer.m_ContainerInfo.m_Attributes[0].m_VertBufferBindingIndex];
	if(m_HasShaders)
		glBindBuffer(GL_ARRAY_BUFFER, BufferObject.m_BufferObjectID);

	if(!m_HasShaders)
	{
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);

		if(IsTextured)
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	if(m_HasShaders)
	{
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, false, BufferContainer.m_ContainerInfo.m_Stride, BufferContainer.m_ContainerInfo.m_Attributes[0].m_pOffset);
		if(IsTextured)
		{
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 3, GL_FLOAT, false, BufferContainer.m_ContainerInfo.m_Stride, BufferContainer.m_ContainerInfo.m_Attributes[1].m_pOffset);
		}

		for(int i = 0; i < pCommand->m_IndicesDrawNum; ++i)
		{
			size_t RealDrawCount = (pCommand->m_pDrawCount[i] / 6) * 4;
			GLint RealOffset = (GLint)((((size_t)(uintptr_t)(pCommand->m_pIndicesOffsets[i])) / (6 * sizeof(unsigned int))) * 4);
			glDrawArrays(GL_QUADS, RealOffset, RealDrawCount);
		}
	}
	else
	{
		glVertexPointer(2, GL_FLOAT, sizeof(m_aStreamVertices[0]), m_aStreamVertices);
		glColorPointer(4, GL_FLOAT, sizeof(m_aStreamVertices[0]), (uint8_t *)m_aStreamVertices + (ptrdiff_t)(sizeof(vec2)));
		if(IsTextured)
			glTexCoordPointer(3, GL_FLOAT, sizeof(m_aStreamVertices[0]), (uint8_t *)m_aStreamVertices + (ptrdiff_t)(sizeof(vec2) + sizeof(vec4)));

		size_t VertexCount = 0;
		for(int i = 0; i < pCommand->m_IndicesDrawNum; ++i)
		{
			size_t RealDrawCount = (pCommand->m_pDrawCount[i] / 6) * 4;
			GLint RealOffset = (GLint)((((size_t)(uintptr_t)(pCommand->m_pIndicesOffsets[i])) / (6 * sizeof(unsigned int))) * 4);
			size_t SingleVertSize = (sizeof(vec2) + (IsTextured ? sizeof(vec3) : 0));
			size_t CurBufferOffset = RealOffset * SingleVertSize;

			for(size_t n = 0; n < RealDrawCount; ++n)
			{
				ptrdiff_t VertOffset = (ptrdiff_t)(CurBufferOffset + (n * SingleVertSize));
				vec2 *pPos = (vec2 *)((uint8_t *)BufferObject.m_pData + VertOffset);
				GL_SVertexTex3D &Vertex = m_aStreamVertices[VertexCount++];
				mem_copy(&Vertex.m_Pos, pPos, sizeof(vec2));
				mem_copy(&Vertex.m_Color, &pCommand->m_Color, sizeof(vec4));
				if(IsTextured)
				{
					vec3 *pTex = (vec3 *)((uint8_t *)BufferObject.m_pData + VertOffset + (ptrdiff_t)sizeof(vec2));
					mem_copy(&Vertex.m_Tex, pTex, sizeof(vec3));
				}

				if(VertexCount >= sizeof(m_aStreamVertices) / sizeof(m_aStreamVertices[0]))
				{
					glDrawArrays(GL_QUADS, 0, VertexCount);
					VertexCount = 0;
				}
			}
		}
		if(VertexCount > 0)
			glDrawArrays(GL_QUADS, 0, VertexCount);
	}

	if(!m_HasShaders)
	{
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);

		if(IsTextured)
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
	else
	{
		glDisableVertexAttribArray(0);
		if(IsTextured)
			glDisableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(0);
	}
}
