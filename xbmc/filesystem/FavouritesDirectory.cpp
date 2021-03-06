/*
 *      Copyright (C) 2005-2013 Team XBMC
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "FavouritesDirectory.h"
#include "File.h"
#include "Util.h"
#include "profiles/ProfilesManager.h"
#include "FileItem.h"
#include "utils/XBMCTinyXML.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "settings/AdvancedSettings.h"
#include "video/VideoInfoTag.h"
#include "URL.h"

namespace XFILE
{


bool CFavouritesDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items)
{
  items.Clear();
  CURL url(strPath);
  
  if (url.GetProtocol() == "favourites")
  {
    Load(items); //load the default favourite files
  }
  return LoadFavourites(strPath, items); //directly load the given file
}
  
bool CFavouritesDirectory::Exists(const char* strPath)
{
  CURL url(strPath);
  
  if (url.GetProtocol() == "favourites")
  {
    return XFILE::CFile::Exists("special://xbmc/system/favourites.xml") 
        || XFILE::CFile::Exists(URIUtils::AddFileToFolder(CProfilesManager::Get().GetProfileUserDataFolder(), "favourites.xml"));
  }
  return XFILE::CFile::Exists(strPath); //directly load the given file
}

bool CFavouritesDirectory::Load(CFileItemList &items)
{
  items.Clear();
  CStdString favourites;

  favourites = "special://xbmc/system/favourites.xml";
  if(XFILE::CFile::Exists(favourites))
    CFavouritesDirectory::LoadFavourites(favourites, items);
  else
    CLog::Log(LOGDEBUG, "CFavourites::Load - no system favourites found, skipping");
  favourites = URIUtils::AddFileToFolder(CProfilesManager::Get().GetProfileUserDataFolder(), "favourites.xml");
  if(XFILE::CFile::Exists(favourites))
    CFavouritesDirectory::LoadFavourites(favourites, items);
  else
    CLog::Log(LOGDEBUG, "CFavourites::Load - no userdata favourites found, skipping");

  return true;
}

bool CFavouritesDirectory::LoadFavourites(const CStdString& strPath, CFileItemList& items)
{
  CXBMCTinyXML doc;
  if (!doc.LoadFile(strPath))
  {
    CLog::Log(LOGERROR, "Unable to load %s (row %i column %i)", strPath.c_str(), doc.Row(), doc.Column());
    return false;
  }
  TiXmlElement *root = doc.RootElement();
  if (!root || strcmp(root->Value(), "favourites"))
  {
    CLog::Log(LOGERROR, "Favourites.xml doesn't contain the <favourites> root element");
    return false;
  }

  TiXmlElement *favourite = root->FirstChildElement("favourite");
  while (favourite)
  {
    // format:
    // <favourite name="Cool Video" thumb="foo.jpg">PlayMedia(c:\videos\cool_video.avi)</favourite>
    // <favourite name="My Album" thumb="bar.tbn">ActivateWindow(MyMusic,c:\music\my album)</favourite>
    // <favourite name="Apple Movie Trailers" thumb="path_to_thumb.png">RunScript(special://xbmc/scripts/apple movie trailers/default.py)</favourite>
    const char *name = favourite->Attribute("name");
    const char *thumb = favourite->Attribute("thumb");
    if (name && favourite->FirstChild())
    {
      if(!items.Contains(favourite->FirstChild()->Value()))
      {
        CFileItemPtr item(new CFileItem(name));
        item->SetPath(favourite->FirstChild()->Value());
        if (thumb) item->SetArt("thumb", thumb);
        items.Add(item);
      }
    }
    favourite = favourite->NextSiblingElement("favourite");
  }
  return true;
}

bool CFavouritesDirectory::Save(const CFileItemList &items)
{
  CStdString favourites;
  CXBMCTinyXML doc;
  TiXmlElement xmlRootElement("favourites");
  TiXmlNode *rootNode = doc.InsertEndChild(xmlRootElement);
  if (!rootNode) return false;

  for (int i = 0; i < items.Size(); i++)
  {
    const CFileItemPtr item = items[i];
    TiXmlElement favNode("favourite");
    favNode.SetAttribute("name", item->GetLabel().c_str());
    if (item->HasArt("thumb"))
      favNode.SetAttribute("thumb", item->GetArt("thumb").c_str());
    TiXmlText execute(item->GetPath());
    favNode.InsertEndChild(execute);
    rootNode->InsertEndChild(favNode);
  }

  favourites = URIUtils::AddFileToFolder(CProfilesManager::Get().GetProfileUserDataFolder(), "favourites.xml");
  return doc.SaveFile(favourites);
}

bool CFavouritesDirectory::AddOrRemove(CFileItem *item, int contextWindow)
{
  if (!item) return false;

  // load our list
  CFileItemList items;
  Load(items);

  CStdString executePath(GetExecutePath(item, contextWindow));

  CFileItemPtr match = items.Get(executePath);
  if (match)
  { // remove the item
    items.Remove(match.get());
  }
  else
  { // create our new favourite item
    CFileItemPtr favourite(new CFileItem(item->GetLabel()));
    if (item->GetLabel().IsEmpty())
      favourite->SetLabel(CUtil::GetTitleFromPath(item->GetPath(), item->m_bIsFolder));
    favourite->SetArt("thumb", item->GetArt("thumb"));
    favourite->SetPath(executePath);
    items.Add(favourite);
  }

  // and save our list again
  return Save(items);
}

bool CFavouritesDirectory::IsFavourite(CFileItem *item, int contextWindow)
{
  CFileItemList items;
  if (!Load(items)) return false;

  return items.Contains(GetExecutePath(item, contextWindow));
}

CStdString CFavouritesDirectory::GetExecutePath(const CFileItem *item, int contextWindow)
{
  CStdString execute;
  if (item->m_bIsFolder && (g_advancedSettings.m_playlistAsFolders ||
                            !(item->IsSmartPlayList() || item->IsPlayList())))
    execute.Format("ActivateWindow(%i,%s)", contextWindow, StringUtils::Paramify(item->GetPath()).c_str());
  else if (item->IsScript())
    execute.Format("RunScript(%s)", StringUtils::Paramify(item->GetPath().Mid(9)).c_str());
  else if (item->IsAndroidApp())
    execute.Format("StartAndroidActivity(%s)", StringUtils::Paramify(item->GetPath().Mid(26)).c_str());
  else  // assume a media file
  {
    if (item->IsVideoDb() && item->HasVideoInfoTag())
      execute.Format("PlayMedia(%s)", StringUtils::Paramify(item->GetVideoInfoTag()->m_strFileNameAndPath).c_str());
    else
      execute.Format("PlayMedia(%s)", StringUtils::Paramify(item->GetPath()).c_str());
  }
  return execute;
}
  
}
