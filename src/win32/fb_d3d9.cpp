/*
** fb_d3d9.cpp
** Code to let ZDoom use Direct3D 9 as a simple framebuffer
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
** This file does _not_ implement hardware-acclerated rendering. It is just
** a means of getting the pixel data to the screen in a more reliable
** method on modern hardware by copying the entire frame to a texture,
** drawing that to the screen, and presenting.
*/

// HEADER FILES ------------------------------------------------------------

#ifdef _DEBUG
#define D3D_DEBUG_INFO
#endif
#define DIRECT3D_VERSION 0x0900
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

#define USE_WINDOWS_DWORD
#include "doomtype.h"

#include "c_dispatch.h"
#include "templates.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "v_pfx.h"
#include "stats.h"
#include "doomerrors.h"

#include "win32iface.h"

#include <mmsystem.h>

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

IMPLEMENT_CLASS(D3DFB)

struct FBVERTEX
{
	FLOAT x, y, z, rhw;
	FLOAT tu, tv;
};
#define D3DFVF_FBVERTEX (D3DFVF_XYZRHW|D3DFVF_TEX1)

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

void DoBlending (const PalEntry *from, PalEntry *to, int count, int r, int g, int b, int a);

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern HWND Window;
extern IVideo *Video;
extern BOOL AppActive;
extern int SessionState;
extern bool VidResizing;

EXTERN_CVAR (Bool, fullscreen)
EXTERN_CVAR (Float, Gamma)
EXTERN_CVAR (Int, vid_displaybits)
EXTERN_CVAR (Bool, vid_vsync)

extern IDirect3D9 *D3D;

extern cycle_t BlitCycles;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

#if 0
// This is the HLSL code:

// Technically, Palette only needs to be a sampler1D, but that
// produces assembly code to copy index.x to index.y, which is
// totally unnecessary.

sampler2D Image : register(s0);
sampler2D Palette : register(s1);
float4 Flash : register(c0);
float4 InvFlash : register(c1);

float4 main (float2 texCoord : TEXCOORD0) : COLOR
{
	float4 index = tex2D (Image, texCoord);
	float4 rgb = tex2D (Palette, index);
	return Flash + rgb * InvFlash;
}
#endif
#if 0
//
// Generated by Microsoft (R) D3DX9 Shader Compiler 9.15.779.0000
//
//   fxc paltex.ps /Tps_1_4 /VnPalTexShaderDef /Fh
//
//
// Parameters:
//
//   float4 Flash;
//   sampler2D Image;
//   float4 InvFlash;
//   sampler2D Palette;
//
//
// Registers:
//
//   Name         Reg   Size
//   ------------ ----- ----
//   Flash        c0       1
//   InvFlash     c1       1
//   Image        s0       1
//   Palette      s1       1
//

    ps_1_4
    texld r0, t0
    phase
    texld r1, r0
    mad r0, r1, c1, c0

// approximately 3 instruction slots used (2 texture, 1 arithmetic)
#endif

const DWORD PalTexShaderDef[] =
{
    0xffff0104, 0x003bfffe, 0x42415443, 0x0000001c, 0x000000b4, 0xffff0104, 
    0x00000004, 0x0000001c, 0x00000100, 0x000000ad, 0x0000006c, 0x00000002, 
    0x00020001, 0x00000074, 0x00000000, 0x00000084, 0x00000003, 0x00000001, 
    0x0000008c, 0x00000000, 0x0000009c, 0x00010002, 0x00020001, 0x00000074, 
    0x00000000, 0x000000a5, 0x00010003, 0x00000001, 0x0000008c, 0x00000000, 
    0x73616c46, 0xabab0068, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 
    0x67616d49, 0xabab0065, 0x000c0004, 0x00010001, 0x00000001, 0x00000000, 
    0x46766e49, 0x6873616c, 0x6c615000, 0x65747465, 0x5f737000, 0x00345f31, 
    0x7263694d, 0x666f736f, 0x52282074, 0x33442029, 0x20395844, 0x64616853, 
    0x43207265, 0x69706d6f, 0x2072656c, 0x35312e39, 0x3937372e, 0x3030302e, 
    0xabab0030, 0x00000042, 0x800f0000, 0xb0e40000, 0x0000fffd, 0x00000042, 
    0x800f0001, 0x80e40000, 0x00000004, 0x800f0000, 0x80e40001, 0xa0e40001, 
    0xa0e40000, 0x0000ffff
};

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// CODE --------------------------------------------------------------------

D3DFB::D3DFB (int width, int height, bool fullscreen)
	: BaseWinFB (width, height)
{
	D3DPRESENT_PARAMETERS d3dpp;
	int i;

	D3DDevice = NULL;
	VertexBuffer = NULL;
	FBTexture = NULL;
	PaletteTexture = NULL;
	PalTexShader = NULL;
	FBFormat = D3DFMT_UNKNOWN;
	PalFormat = D3DFMT_UNKNOWN;
	VSync = vid_vsync;
	OffByOneAt = -1;
	PixelDoubling = 0;

	Gamma = 1.0;
	memset (FlashConstants, 0, sizeof(FlashConstants));
	FlashConstants[1][3] = 1.f;		// Always use alpha from palette (which is always 1, so meh)
	FlashColor = 0;
	FlashAmount = 0;

	NeedGammaUpdate = false;
	NeedPalUpdate = false;

	if (MemBuffer == NULL)
	{
		return;
	}

	for (i = 0; i < 256; i++)
	{
		GammaTable[i] = i;
	}
	memcpy (SourcePalette, GPalette.BaseColors, sizeof(PalEntry)*256);

	Windowed = !(static_cast<Win32Video *>(Video)->GoFullscreen (fullscreen));

	TrueHeight = height;
	if (fullscreen)
	{
		for (Win32Video::ModeInfo *mode = static_cast<Win32Video *>(Video)->m_Modes; mode != NULL; mode = mode->next)
		{
			if (mode->width == Width && mode->height == Height)
			{
				TrueHeight = mode->realheight;
				PixelDoubling = mode->doubling;
				break;
			}
		}
	}

	FillPresentParameters (&d3dpp, fullscreen, VSync);

	HRESULT hr;

	if (FAILED(hr = D3D->CreateDevice (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window,
		D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &d3dpp, &D3DDevice)))
	{
		D3DDevice = NULL;
		if (fullscreen)
		{
			d3dpp.BackBufferFormat = D3DFMT_R5G6B5;
			if (FAILED(D3D->CreateDevice (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window,
				D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &D3DDevice)))
			{
				D3DDevice = NULL;
			}
		}
	}
	if (D3DDevice != NULL)
	{
		CreateResources ();
	}
}

D3DFB::~D3DFB ()
{
	ReleaseResources ();
	if (D3DDevice != NULL)
	{
		D3DDevice->Release();
	}
}

void D3DFB::FillPresentParameters (D3DPRESENT_PARAMETERS *pp, bool fullscreen, bool vsync)
{
	memset (pp, 0, sizeof(*pp));
	pp->Windowed = !fullscreen;
	pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp->BackBufferWidth = Width << PixelDoubling;
	pp->BackBufferHeight = TrueHeight << PixelDoubling;
	pp->BackBufferFormat = fullscreen ? D3DFMT_X8R8G8B8 : D3DFMT_UNKNOWN;
	pp->BackBufferCount = 1;
	pp->hDeviceWindow = Window;
	pp->PresentationInterval = vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
}

bool D3DFB::CreateResources ()
{
	if (!Windowed)
	{
		// Remove the window border in fullscreen mode
		SetWindowLongPtr (Window, GWL_STYLE, WS_POPUP|WS_VISIBLE|WS_SYSMENU);
		ShowWindow (Window, SW_SHOW);
	}
	else
	{
		// Resize the window to match desired dimensions
		int sizew = Width + GetSystemMetrics (SM_CXSIZEFRAME)*2;
		int sizeh = Height + GetSystemMetrics (SM_CYSIZEFRAME) * 2 +
					 GetSystemMetrics (SM_CYCAPTION);
		LOG2 ("Resize window to %dx%d\n", sizew, sizeh);
		VidResizing = true;
		// Make sure the window has a border in windowed mode
		SetWindowLongPtr (Window, GWL_STYLE, WS_VISIBLE|WS_OVERLAPPEDWINDOW);
		if (GetWindowLong (Window, GWL_EXSTYLE) & WS_EX_TOPMOST)
		{
			// Direct3D 9 will apparently add WS_EX_TOPMOST to fullscreen windows,
			// and removing it is a little tricky. Using SetWindowLongPtr to clear it
			// will not do the trick, but sending the window behind everything will.
			SetWindowPos (Window, HWND_BOTTOM, 0, 0, sizew, sizeh,
				SWP_DRAWFRAME | SWP_NOCOPYBITS | SWP_NOMOVE);
			SetWindowPos (Window, HWND_TOP, 0, 0, 0, 0, SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOSIZE);
		}
		else
		{
			SetWindowPos (Window, NULL, 0, 0, sizew, sizeh,
				SWP_DRAWFRAME | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
		}
		VidResizing = false;
		ShowWindow (Window, SW_SHOWNORMAL);
	}
	if (FAILED(D3DDevice->CreatePixelShader (PalTexShaderDef, &PalTexShader)))
	{
		return false;
	}
	if (!CreateFBTexture() || !CreatePaletteTexture())
	{
		return false;
	}
	if (!CreateVertexes())
	{
		return false;
	}
	SetGamma (Gamma);
	return true;
}

void D3DFB::ReleaseResources ()
{
	if (FBTexture != NULL)
	{
		FBTexture->Release();
		FBTexture = NULL;
	}
	if (VertexBuffer != NULL)
	{
		VertexBuffer->Release();
		VertexBuffer = NULL;
	}
	if (PaletteTexture != NULL)
	{
		PaletteTexture->Release();
		PaletteTexture = NULL;
	}
	if (PalTexShader != NULL)
	{
		PalTexShader->Release();
		PalTexShader = NULL;
	}
}

bool D3DFB::Reset ()
{
	D3DPRESENT_PARAMETERS d3dpp;

	// Free resources created with D3DPOOL_DEFAULT.
	if (FBTexture != NULL)
	{
		FBTexture->Release();
		FBTexture = NULL;
	}
	if (VertexBuffer != NULL)
	{
		VertexBuffer->Release();
		VertexBuffer = NULL;
	}
	FillPresentParameters (&d3dpp, !Windowed, VSync);
	if (!SUCCEEDED(D3DDevice->Reset (&d3dpp)))
	{
		return false;
	}
	if (!CreateFBTexture() || !CreateVertexes())
	{
		return false;
	}
	if (OffByOneAt < 256)
	{
		D3DDevice->SetSamplerState (1, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
		D3DDevice->SetSamplerState (1, D3DSAMP_BORDERCOLOR,
			D3DCOLOR_XRGB(GammaTable[SourcePalette[255].r],
						  GammaTable[SourcePalette[255].g],
						  GammaTable[SourcePalette[255].b]));
	}
	return true;
}

//==========================================================================
//
// DoOffByOneCheck
//
// Since NVidia hardware has an off-by-one error in the pixel shader.
// On a Geforce 7950GT and a 6200, I have witnessed it skip palette entry
// 240. I have a report that an FX card skips in a totally different spot.
// So rather than try and correct it in the shader, we detect it here and
// compensate when uploading the palette and when drawing by setting the
// sampler mode for the palette to border and making the border color the
// final color in the palette.
//
// Interestingly, a Radeon x300 doesn't have this problem. I am curious
// if other ATI hardware is the same.
//
//==========================================================================

void D3DFB::DoOffByOneCheck ()
{
	IDirect3DSurface9 *savedrendertarget;
	IDirect3DSurface9 *testsurf, *readsurf;
	D3DSURFACE_DESC desc;
	D3DLOCKED_RECT lockrect;
	RECT testrect = { 0, 0, 256, 1 };
	float texright = 256.f / float(FBWidth);
	float texbot = 1.f / float(FBHeight);
	FBVERTEX verts[4] =
	{
		{ -0.5f,  -0.5f, 0.5f, 1.f,      0.f,    0.f },
		{ 255.5f, -0.5f, 0.5f, 1.f, texright,    0.f },
		{ 255.5f,  0.5f, 0.5f, 1.f, texright, texbot },
		{ -0.5f,   0.5f, 0.5f, 1.f,      0.f, texbot }
	};
	float flash[2][4] =
	{
		{ 0.f, 0.f, 0.f, 0.f },
		{ 1.f, 1.f, 1.f, 1.f }
	};

	union
	{
		BYTE Pal32[256][4];
		WORD Pal16[256];
	};
	int i, c;

	if (OffByOneAt >= 0)
	{
		return;
	}

	// Create an easily recognizable R3G3B2 palette.
	if (PalFormat == D3DFMT_A8R8G8B8)
	{
		for (i = 0; i < 256; ++i)
		{
			Pal32[i][0] = (i & 0x03) << 6;		// blue
			Pal32[i][1] = (i & 0x1C) << 3;		// green
			Pal32[i][2] = (i & 0xE0);			// red;
			Pal32[i][3] = 255;
		}
	}
	else
	{
		for (i = 0; i < 256; ++i)
		{
			Pal16[i] = ((i & 0xE0) << 8) |		// red
					   ((i & 0x1C) << 6) |		// green
					   ((i & 0x03) << 3);		// blue
		}
	}
	// Upload the palette
	if (SUCCEEDED(PaletteTexture->LockRect (0, &lockrect, NULL, 0)))
	{
		memcpy (lockrect.pBits, Pal32, 256 * ((PalFormat == D3DFMT_A8R8G8B8) ? 4 : 2));
		PaletteTexture->UnlockRect (0);
	}
	else
	{
		return;
	}
	// Prepare a texture with values 0-256.
	if (SUCCEEDED(FBTexture->LockRect (0, &lockrect, &testrect, 0)))
	{
		for (i = 0; i < 256; ++i)
		{
			((BYTE *)lockrect.pBits)[i] = i;
		}
		FBTexture->UnlockRect (0);
	}
	else
	{
		return;
	}
	// Create a render target that we can draw it to.
	if (FAILED(D3DDevice->GetRenderTarget (0, &savedrendertarget)))
	{
		return;
	}
	if (FAILED(D3DDevice->CreateRenderTarget (256, 1, PalFormat, D3DMULTISAMPLE_NONE, 0, FALSE, &testsurf, NULL)))
	{
		return;
	}
	if (FAILED(D3DDevice->CreateOffscreenPlainSurface (256, 1, PalFormat, D3DPOOL_SYSTEMMEM, &readsurf, NULL)))
	{
		testsurf->Release();
		return;
	}
	if (FAILED(D3DDevice->SetRenderTarget (0, testsurf)))
	{
		testsurf->Release();
		readsurf->Release();
		return;
	}
	// Write it to the render target using the pixel shader.
	D3DDevice->BeginScene();
	D3DDevice->SetTexture (0, FBTexture);
	D3DDevice->SetTexture (1, PaletteTexture);
	D3DDevice->SetFVF (D3DFVF_FBVERTEX);
	D3DDevice->SetPixelShader (PalTexShader);
	D3DDevice->SetPixelShaderConstantF (0, flash[0], 2);
	D3DDevice->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts, sizeof(FBVERTEX));
	D3DDevice->EndScene();
	D3DDevice->SetRenderTarget (0, savedrendertarget);
	savedrendertarget->Release();
	// Now read it back and see where it skips an entry
	if (SUCCEEDED(D3DDevice->GetRenderTargetData (testsurf, readsurf)) &&
		SUCCEEDED(readsurf->LockRect (&lockrect, &testrect, D3DLOCK_READONLY)))
	{
		desc.Format = PalFormat;
		if (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8)
		{
			const BYTE *pix = (const BYTE *)lockrect.pBits;
			for (i = 0; i < 256; ++i, pix += 4)
			{
				c = (pix[0] >> 6) |					// blue
					((pix[1] >> 5) << 2) |			// green
					((pix[2] >> 5) << 5);			// red
				if (c != i)
				{
					break;
				}
			}
		}
		else if (desc.Format == D3DFMT_A1R5G5B5 || desc.Format == D3DFMT_X1R5G5B5)
		{
			const WORD *pix = (const WORD *)lockrect.pBits;
			for (i = 0; i < 256; ++i, ++pix)
			{
				c = ((*pix & 0x0018) >> 3) |			// blue
					((*pix & 0x0380) >> 5) |			// green
					((*pix & 0x7C00) >> 7) ;			// red
				if (c != i)
				{
					break;
				}
			}
		}
		else if (desc.Format == D3DFMT_R5G6B5)
		{
			const WORD *pix = (const WORD *)lockrect.pBits;
			for (i = 0; i < 256; ++i, ++pix)
			{
				c = ((*pix & 0x0018) >> 3) |			// blue
					((*pix & 0x0700) >> 6) |			// green
					((*pix & 0xE000) >> 8) ;			// red
				if (c != i)
				{
					break;
				}
			}
		}
		else
		{
			// Huh? What kind of backbuffer is this?
			i = 256;
		}
	}
	readsurf->UnlockRect();
	readsurf->Release();
	testsurf->Release();
	OffByOneAt = i;
	if (i < 256)
	{
		D3DDevice->SetSamplerState (1, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
	}
}

bool D3DFB::CreateFBTexture ()
{
	if (FAILED(D3DDevice->CreateTexture (Width, Height, 1, D3DUSAGE_DYNAMIC, D3DFMT_L8, D3DPOOL_DEFAULT, &FBTexture, NULL)))
	{
		int pow2width, pow2height, i;

		for (i = 1; i < Width; i <<= 1) {} pow2width = i;
		for (i = 1; i < Height; i <<= 1) {} pow2height = i;

		if (FAILED(D3DDevice->CreateTexture (pow2width, pow2height, 1, D3DUSAGE_DYNAMIC, D3DFMT_L8, D3DPOOL_DEFAULT, &FBTexture, NULL)))
		{
			return false;
		}
		else
		{
			FBWidth = pow2width;
			FBHeight = pow2height;
		}
	}
	else
	{
		FBWidth = Width;
		FBHeight = Height;
	}
	return true;
}

bool D3DFB::CreatePaletteTexture ()
{
	if (FAILED(D3DDevice->CreateTexture (256, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &PaletteTexture, NULL)))
	{
		if (FAILED(D3DDevice->CreateTexture (256, 1, 1, 0, D3DFMT_R5G6B5, D3DPOOL_MANAGED, &PaletteTexture, NULL)))
		{
			return false;
		}
		else
		{
			PalFormat = D3DFMT_R5G6B5;
		}
	}
	else
	{
		PalFormat = D3DFMT_A8R8G8B8;
	}
	return true;
}

bool D3DFB::CreateVertexes ()
{
	float top = (TrueHeight - Height) * 0.5f - 0.5f;
	float right = float(Width << PixelDoubling) - 0.5f;
	float bot = float(Height << PixelDoubling) + top + 1.f;
	float texright = float(Width) / float(FBWidth);
	float texbot = float(Height) / float(FBHeight);
	FBVERTEX verts[4] =
	{
		{ -0.5f, top, 0.5f, 1.f,      0.f,    0.f },
		{ right, top, 0.5f, 1.f, texright,    0.f },
		{ right, bot, 0.5f, 1.f, texright, texbot },
		{ -0.5f, bot, 0.5f, 1.f,      0.f, texbot }
	};
	void *pverts;

	if (FAILED(D3DDevice->CreateVertexBuffer (sizeof(verts), D3DUSAGE_WRITEONLY, D3DFVF_FBVERTEX, D3DPOOL_DEFAULT, &VertexBuffer, NULL)) ||
		FAILED(VertexBuffer->Lock (0, sizeof(verts), &pverts, 0)))
	{
		return false;
	}
	else
	{
		memcpy (pverts, verts, sizeof(verts));
		VertexBuffer->Unlock();
	}
	return true;
}

int D3DFB::GetPageCount ()
{
	return 1;
}

void D3DFB::PaletteChanged ()
{
}

int D3DFB::QueryNewPalette ()
{
	return 0;
}

bool D3DFB::IsValid ()
{
	return D3DDevice != NULL;
}

HRESULT D3DFB::GetHR ()
{
	return 0;
}

bool D3DFB::IsFullscreen ()
{
	return !Windowed;
}

bool D3DFB::Lock ()
{
	return Lock(true);
}

bool D3DFB::Lock (bool buffered)
{
	if (LockCount++ > 0)
	{
		return false;
	}

	Buffer = MemBuffer;
	return false;
}

void D3DFB::Unlock ()
{
	LOG1 ("Unlock     <%d>\n", LockCount);

	if (LockCount == 0)
	{
		return;
	}

	if (UpdatePending && LockCount == 1)
	{
		Update ();
	}
	else if (--LockCount == 0)
	{
		Buffer = NULL;
	}
}

void D3DFB::Update ()
{
	bool pchanged = false;

	LOG3 ("Update     <%d,%c:%d>\n", LockCount, AppActive?'Y':'N', SessionState);

	if (LockCount != 1)
	{
		//I_FatalError ("Framebuffer must have exactly 1 lock to be updated");
		if (LockCount > 0)
		{
			UpdatePending = true;
			--LockCount;
		}
		return;
	}

	DrawRateStuff ();

	if (NeedGammaUpdate)
	{
		NeedGammaUpdate = false;
		CalcGamma (Gamma, GammaTable);
		NeedPalUpdate = true;
	}
	
	if (NeedPalUpdate)
	{
		UploadPalette();
	}

	BlitCycles = 0;
	clock (BlitCycles);

	LockCount = 0;
	PaintToWindow ();

	unclock (BlitCycles);
	LOG1 ("cycles = %d\n", BlitCycles);

	Buffer = NULL;
	UpdatePending = false;
}

bool D3DFB::PaintToWindow ()
{
	RECT texrect = { 0, 0, Width, Height };
	D3DLOCKED_RECT lockrect;
	HRESULT hr;

	if (LockCount != 0)
	{
		return false;
	}
	hr = D3DDevice->TestCooperativeLevel();
	if (FAILED(hr))
	{
		if (hr != D3DERR_DEVICENOTRESET || !Reset())
		{
			Sleep (1);
			return false;
		}
	}
	if ((FBWidth == Width && FBHeight == Height && SUCCEEDED(FBTexture->LockRect (0, &lockrect, NULL, D3DLOCK_DISCARD))) ||
		SUCCEEDED(FBTexture->LockRect (0, &lockrect, &texrect, 0)))
	{
		if (lockrect.Pitch == Pitch)
		{
			memcpy (lockrect.pBits, MemBuffer, Width * Height);
		}
		else
		{
			BYTE *dest = (BYTE *)lockrect.pBits;
			BYTE *src = MemBuffer;
			for (int y = 0; y < Height; y++)
			{
				memcpy (dest, src, Width);
				dest += lockrect.Pitch;
				src += Pitch;
			}
		}
		FBTexture->UnlockRect (0);
	}
	if (TrueHeight != Height)
	{
		// Letterbox! Draw black top and bottom borders.
		int topborder = (TrueHeight - Height) / 2;
		D3DRECT rects[2] = { { 0, 0, Width, topborder }, { 0, Height + topborder, Width, TrueHeight } };
		D3DDevice->Clear (2, rects, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0,0,0), 1.f, 0);
	}
	D3DDevice->BeginScene();
	D3DDevice->SetTexture (0, FBTexture);
	D3DDevice->SetTexture (1, PaletteTexture);
	D3DDevice->SetStreamSource (0, VertexBuffer, 0, sizeof(FBVERTEX));
	D3DDevice->SetFVF (D3DFVF_FBVERTEX);
	D3DDevice->SetPixelShader (PalTexShader);
	D3DDevice->SetPixelShaderConstantF (0, FlashConstants[0], 2);
	D3DDevice->DrawPrimitive (D3DPT_TRIANGLEFAN, 0, 2);
	D3DDevice->EndScene();
	return SUCCEEDED(D3DDevice->Present(NULL, NULL, NULL, NULL));
}

void D3DFB::UploadPalette ()
{
	D3DLOCKED_RECT lockrect;
	int i;

	if (OffByOneAt < 0)
	{
		DoOffByOneCheck ();
	}
	if (SUCCEEDED(PaletteTexture->LockRect (0, &lockrect, NULL, 0)))
	{
		// Keep trying to update the palette if we haven't done the off-by-one
		// check yet. Otherwise, wait until the next time the palette changes.
		NeedPalUpdate = (OffByOneAt < 0);

		if (PalFormat == D3DFMT_A8R8G8B8)
		{
			BYTE *pix = (BYTE *)lockrect.pBits;
			for (i = 0; i < OffByOneAt; ++i, pix += 4)
			{
				pix[0] = GammaTable[SourcePalette[i].b];
				pix[1] = GammaTable[SourcePalette[i].g];
				pix[2] = GammaTable[SourcePalette[i].r];
				pix[3] = 255;
			}
			for (; i < 256; ++i, pix += 4)
			{
				pix[0] = GammaTable[SourcePalette[i-1].b];
				pix[1] = GammaTable[SourcePalette[i-1].g];
				pix[2] = GammaTable[SourcePalette[i-1].r];
				pix[3] = 255;
			}
		}
		else
		{
			WORD *pix = (WORD *)lockrect.pBits;
			for (i = 0; i < OffByOneAt; ++i, ++pix)
			{
				*pix = ((GammaTable[SourcePalette[i].r] >> 3) << 11) |
					   ((GammaTable[SourcePalette[i].g] >> 2) << 5) |
					    (GammaTable[SourcePalette[i].b] >> 3);
			}
			for (; i < 256; ++i, ++pix)
			{
				*pix = ((GammaTable[SourcePalette[i-1].r] >> 3) << 11) |
					   ((GammaTable[SourcePalette[i-1].g] >> 2) << 5) |
					    (GammaTable[SourcePalette[i-1].b] >> 3);
			}
		}
		PaletteTexture->UnlockRect (0);
	}
	if (OffByOneAt < 256)
	{
		D3DDevice->SetSamplerState (1, D3DSAMP_BORDERCOLOR,
			D3DCOLOR_XRGB(GammaTable[SourcePalette[255].r],
						  GammaTable[SourcePalette[255].g],
						  GammaTable[SourcePalette[255].b]));
	}
}

PalEntry *D3DFB::GetPalette ()
{
	return SourcePalette;
}

void D3DFB::UpdatePalette ()
{
	NeedPalUpdate = true;
}

bool D3DFB::SetGamma (float gamma)
{
	LOG1 ("SetGamma %g\n", gamma);
	Gamma = gamma;
	NeedGammaUpdate = true;
	return true;
}

bool D3DFB::SetFlash (PalEntry rgb, int amount)
{
	FlashColor = rgb;
	FlashAmount = amount;

	// Fill in the constants for the pixel shader to do linear interpolation between the palette and the flash:
	float r = rgb.r / 255.f, g = rgb.g / 255.f, b = rgb.b / 255.f, a = amount / 256.f;
	FlashConstants[0][0] = r * a;
	FlashConstants[0][1] = g * a;
	FlashConstants[0][2] = b * a;
	a = 1 - a;
	FlashConstants[1][0] = a;
	FlashConstants[1][1] = a;
	FlashConstants[1][2] = a;
	return true;
}

void D3DFB::GetFlash (PalEntry &rgb, int &amount)
{
	rgb = FlashColor;
	amount = FlashAmount;
}

void D3DFB::GetFlashedPalette (PalEntry pal[256])
{
	memcpy (pal, SourcePalette, 256*sizeof(PalEntry));
	if (FlashAmount)
	{
		DoBlending (pal, pal, 256, FlashColor.r, FlashColor.g, FlashColor.b, FlashAmount);
	}
}

void D3DFB::SetVSync (bool vsync)
{
	if (VSync != vsync)
	{
		VSync = vsync;
		Reset();
	}
}

void D3DFB::Blank ()
{
	// Only used by movie player, which isn't working with D3D9 yet.
}
