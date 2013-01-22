/*
 *      Copyright (C) 2013 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#include "freeimage.h"
#include "utils/log.h"

#ifdef TARGET_WINDOWS
#pragma comment(lib, "FreeImage.lib")
#endif

void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message)
{
  CLog::Log(LOGERROR, "FreeImageError: Format %s, %s", (fif != FIF_UNKNOWN) ? FreeImage_GetFormatFromFIF(fif) : "unknown", message );
}

CFreeImage::CFreeImage(const std::string& strMimeType): m_strMimeType(strMimeType), m_thumbnailbuffer(NULL), m_fibitmap(NULL)
{
  // call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
  FreeImage_Initialise();
#endif // FREEIMAGE_LIB
  m_hasAlpha = false;
  FreeImage_SetOutputMessage(FreeImageErrorHandler);
}

CFreeImage::~CFreeImage()
{
  if(m_fibitmap != NULL)
    FreeImage_Unload(m_fibitmap);
  ReleaseThumbnailBuffer();
#ifdef FREEIMAGE_LIB
  FreeImage_DeInitialise();
#endif // FREEIMAGE_LIB
}

bool CFreeImage::LoadImageFromMemory(unsigned char* buffer, unsigned int bufSize, unsigned int width, unsigned int height)
{
  FREE_IMAGE_FORMAT fif = GetFIF();
  if(fif == FIF_UNKNOWN)
    return false;

  // attach the binary data to a memory stream
  FIMEMORY *hmem = FreeImage_OpenMemory(buffer, bufSize);

  // load an image from the memory stream
  FIBITMAP* src = FreeImage_LoadFromMemory(fif, hmem, 0);
  if(src == NULL)
  {
    FreeImage_CloseMemory(hmem);
    return false;
  }
  m_fibitmap = FreeImage_ConvertTo32Bits(src);

  m_hasAlpha = FreeImage_GetColorType(m_fibitmap) == FIC_RGBALPHA ? true : false;
  m_width = FreeImage_GetWidth(m_fibitmap);
  m_height = FreeImage_GetHeight(m_fibitmap);
  m_orientation = GetExifOrientation(m_fibitmap);
  m_originalWidth = width;
  m_originalHeight = height;

  FreeImage_Unload(src);
  FreeImage_CloseMemory(hmem);
  return true;
}

bool CFreeImage::Decode(const unsigned char *pixels, unsigned int pitch, unsigned int format)
{
  FIBITMAP* bitmap2 = FreeImage_ConvertTo32Bits(m_fibitmap);
  FreeImage_ConvertToRawBits((BYTE*)pixels, bitmap2, pitch, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK, TRUE);
  FreeImage_Unload(m_fibitmap);
  m_fibitmap = NULL;
  return true;
}

bool CFreeImage::CreateThumbnailFromSurface(unsigned char* bufferin, unsigned int width, unsigned int height, unsigned int format, unsigned int pitch,
                                         unsigned char* &bufferout, unsigned int &bufferoutSize)
{
  if (!bufferin)
    return false;

  FREE_IMAGE_FORMAT fif = GetFIF();
  if(fif == FIF_UNKNOWN)
    return false;

  FIBITMAP* bitmap = FreeImage_ConvertFromRawBits(bufferin, width, height, pitch, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK, TRUE);
  FIBITMAP* bitmap2 = FreeImage_ConvertTo24Bits(bitmap);
  FreeImage_Unload(bitmap);

  m_thumbnailbuffer = FreeImage_OpenMemory();
  if(FreeImage_SaveToMemory(fif, bitmap2, m_thumbnailbuffer, 0) != TRUE)
  {
    ReleaseThumbnailBuffer();
    FreeImage_Unload(bitmap2);
    return false;
  }

  DWORD size = 0;
  FreeImage_AcquireMemory(m_thumbnailbuffer, &bufferout, &size);
  FreeImage_Unload(bitmap2);
  bufferoutSize = size;
  return true;
}

void CFreeImage::ReleaseThumbnailBuffer()
{
  if(m_thumbnailbuffer != NULL)
  {
    FreeImage_CloseMemory(m_thumbnailbuffer);
    m_thumbnailbuffer = NULL;
  }
}

unsigned int CFreeImage::GetExifOrientation(FIBITMAP *dib)
{
  // check for Exif rotation
  if(FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, dib))
  {
    FITAG *tag = NULL;
    FreeImage_GetMetadata(FIMD_EXIF_MAIN, dib, "Orientation", &tag);
    if(tag != NULL)
      return (unsigned int)(*((unsigned short*)FreeImage_GetTagValue(tag)));
  }
  return 0;
}

FREE_IMAGE_FORMAT CFreeImage::GetFIF()
{
  FREE_IMAGE_FORMAT fif = FreeImage_GetFIFFromMime(m_strMimeType.c_str());
  if(fif == FIF_UNKNOWN)
  {
    std::string strExt = m_strMimeType;
    int nPos = strExt.find('/');
    if (nPos > -1)
      strExt.erase(0, nPos + 1);
    // try to guess the file format from the file extension
    return FreeImage_GetFIFFromFilename(strExt.c_str());
  }
  return fif;
}