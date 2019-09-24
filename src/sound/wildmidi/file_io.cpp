/*
** file_io.cpp
** ZDoom compatible file IO interface for WildMIDI
** (This file was completely redone to remove the low level IO code references)
**
**---------------------------------------------------------------------------
** Copyright 2010 Christoph Oelckers
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
*/

#include <errno.h>
#include <memory>

#include "files.h"
#include "wm_error.h"
#include "file_io.h"
#include "i_soundfont.h"

std::unique_ptr<FSoundFontReader> wm_sfreader;


unsigned char *_WM_BufferFile(const char *filename, unsigned long int *size) 
{
	FileReader fp;

	if (filename == nullptr)
	{
		fp = wm_sfreader->OpenMainConfigFile();
		filename = wm_sfreader->MainConfigFileName();
	}
	else
	{
		fp = wm_sfreader->OpenFile(filename);
	}

	if (!fp.isOpen())
	{
		_WM_ERROR(__FUNCTION__, __LINE__, WM_ERR_LOAD, filename, errno);
		return NULL;
	}

	auto fsize = fp.GetLength();

	if (fsize > WM_MAXFILESIZE) 
	{
		/* don't bother loading suspiciously long files */
		_WM_ERROR(__FUNCTION__, __LINE__, WM_ERR_LONGFIL, filename, 0);
		return NULL;
	}

	unsigned char *data = (unsigned char*)malloc(fsize+1);
	if (data == NULL)
	{
		_WM_ERROR(__FUNCTION__, __LINE__, WM_ERR_MEM, NULL, errno);
		_WM_ERROR(__FUNCTION__, __LINE__, WM_ERR_LOAD, filename, errno);
		return NULL;
	}

	fp.Read(data, fsize);
	data[fsize] = 0;
	*size = (long)fsize;
	return data;
}
