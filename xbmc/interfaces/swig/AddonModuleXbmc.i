/*
 *      Copyright (C) 2005-2012 Team XBMC
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

%module(directors="1") xbmc

%{
#include "interfaces/native/Player.h"
#include "interfaces/native/RenderCapture.h"
#include "interfaces/native/Keyboard.h"
#include "interfaces/native/ModuleXbmc.h"
#include "interfaces/native/Monitor.h"

using namespace XBMCAddon;
using namespace xbmc;

#if defined(__GNUG__) && (__GNUC__>4) || (__GNUC__==4 && __GNUC_MINOR__>=2)
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
%}

%include "interfaces/native/swighelper.h"

%feature("python:coerceToUnicode") XBMCAddon::xbmc::getLocalizedString "true"

%include "interfaces/native/ModuleXbmc.h"

%feature("director") Player;
%feature("ref") Player "${ths}->Acquire();"
%feature("unref") Player "${ths}->Release();"

%feature("python:method:play") Player
{
    PyObject *pObject = NULL;
    PyObject *pObjectListItem = NULL;
    char bWindowed = false;
    static const char *keywords[] = { "item", "listitem", "windowed", NULL };

    if (!PyArg_ParseTupleAndKeywords(
      args,
      kwds,
      (char*)"|OOb",
      (char**)keywords,
      &pObject,
      &pObjectListItem,
      &bWindowed))
    {
      return NULL;
    }

    try
    {
      Player* player = ((Player*)retrieveApiInstance((PyObject*)self,&PyXBMCAddon_xbmc_Player_Type,"play","XBMCAddon::xbmc::Player"));

      // set fullscreen or windowed
      bool windowed = (0 != bWindowed);

      if (pObject == NULL)
        player->playCurrent(windowed);
      else if ((PyString_Check(pObject) || PyUnicode_Check(pObject)))
      {
        CStdString item;
        PyXBMCGetUnicodeString(item,pObject,"item","Player::play");
        XBMCAddon::xbmcgui::ListItem* pListItem = 
          (pObjectListItem ? 
           (XBMCAddon::xbmcgui::ListItem *)retrieveApiInstance(pObjectListItem,"p.XBMCAddon::xbmcgui::ListItem","XBMCAddon::xbmcgui::","play") :
           NULL);
        player->playStream(item,pListItem,windowed);
      }
      else // pObject must be a playlist
        player->playPlaylist((PlayList *)retrieveApiInstance(pObject,"p.PlayList","XBMCAddon::xbmcgui::", "play"), windowed);
    }
    catch (XBMCAddon::Exception e)
    { 
      CLog::Log(LOGERROR,"Leaving Python method 'XBMCAddon_xbmc_Player_play'. Exception from call to 'play' '%s' ... returning NULL", e.getMessage().c_str());
      PyErr_SetString(PyExc_RuntimeError, e.getMessage().c_str()); 
      return NULL; 
    }
    catch (...)
    {
      CLog::Log(LOGERROR,"Unknown exception thrown from the call 'play'");
      PyErr_SetString(PyExc_RuntimeError, "Unknown exception thrown from the call 'play'"); 
      return NULL; 
    }

    Py_INCREF(Py_None);
    return Py_None;
  }

%include "interfaces/native/Player.h"

 // TODO: This needs to be done with a class that holds the Image
 // data. A memory buffer type. Then a typemap needs to be defined
 // for that type.
%feature("python:method:getImage") RenderCapture
{
  RenderCapture* rc = ((RenderCapture*)retrieveApiInstance((PyObject*)self,&PyXBMCAddon_xbmc_RenderCapture_Type,"getImage","XBMCAddon::xbmc::RenderCapture"));
  if (rc->GetUserState() != CAPTURESTATE_DONE)
  {
    PyErr_SetString(PyExc_SystemError, "illegal user state");
    return NULL;
  }
  
  Py_ssize_t size = rc->getWidth() * rc->getHeight() * 4;
  return PyByteArray_FromStringAndSize((const char *)rc->GetPixels(), size);
}

%include "interfaces/native/RenderCapture.h"

%include "interfaces/native/InfoTagMusic.h"
%include "interfaces/native/InfoTagVideo.h"
%include "interfaces/native/Keyboard.h"
%include "interfaces/native/PlayList.h"

%feature("director") Monitor;
%feature("ref") Monitor "${ths}->Acquire();"
%feature("unref") Monitor "${ths}->Release();"

%include "interfaces/native/Monitor.h"


