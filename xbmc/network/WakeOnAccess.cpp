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

#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "system.h"
#include "network/Network.h"
#include "Application.h"
#include "DNSNameCache.h"
#include "dialogs/GUIDialogProgress.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "utils/JobManager.h"
#include "utils/log.h"
#include "utils/XMLUtils.h"

#include "WakeOnAccess.h"

using namespace std;

#define DEFAULT_NETWORK_INIT_SEC      (20)   // wait 20 sec for network after startup or resume
#define DEFAULT_NETWORK_SETTLE_MS     (500)  // require 500ms of consistent network availability before trusting it

#define DEFAULT_TIMEOUT_SEC (5*60)           // at least 5 minutes between each magic packets
#define DEFAULT_WAIT_FOR_ONLINE_SEC_1 (40)   // wait at 40 seconds after sending magic packet
#define DEFAULT_WAIT_FOR_ONLINE_SEC_2 (40)   // same for extended wait
#define DEFAULT_WAIT_FOR_SERVICES_SEC (5)    // wait 5 seconds after host go online to launch file sharing deamons

static int GetTotalSeconds(const CDateTimeSpan& ts)
{
  int hours = ts.GetHours() + ts.GetDays() * 24;
  int minutes = ts.GetMinutes() + hours * 60;
  return ts.GetSeconds() + minutes * 60;
}

static unsigned long HostToIP(const CStdString& host)
{
  CStdString ip;
  CDNSNameCache::Lookup(host, ip);
  return inet_addr(ip.c_str());
}

CWakeOnAccess::WakeUpEntry::WakeUpEntry (bool isAwake)
  : timeout (0, 0, 0, DEFAULT_TIMEOUT_SEC)
  , wait_online1_sec(DEFAULT_WAIT_FOR_ONLINE_SEC_1)
  , wait_online2_sec(DEFAULT_WAIT_FOR_ONLINE_SEC_2)
  , wait_services_sec(DEFAULT_WAIT_FOR_SERVICES_SEC)
{
  nextWake = CDateTime::GetCurrentDateTime();

  if (isAwake)
    nextWake += timeout;
}

CWakeOnAccess::CMACDiscoveryJob::CMACDiscoveryJob(const CStdString& host)
  : m_host(host)
{
}

bool CWakeOnAccess::CMACDiscoveryJob::DoWork()
{
  unsigned long ipAddress = HostToIP(m_host);

  if (ipAddress == INADDR_NONE)
  {
    CLog::Log(LOGERROR, "%s - can't determine ip of '%s'", __FUNCTION__, m_host.c_str());
    return false;
  }

  vector<CNetworkInterface*>& ifaces = g_application.getNetwork().GetInterfaceList();
  for (vector<CNetworkInterface*>::const_iterator it = ifaces.begin(); it != ifaces.end(); ++it)
  {
    if ((*it)->GetHostMacAddress(ipAddress, m_macAddres))
      return true;
  }

  return false;
}

const CStdString& CWakeOnAccess::CMACDiscoveryJob::GetMAC() const
{
  return m_macAddres;
}

const CStdString& CWakeOnAccess::CMACDiscoveryJob::GetHost() const
{
  return m_host;
}

class WaitCondition
{
public:
  virtual bool SuccessWaiting () const { return false; }
};

class ProgressDialogHelper
{
public:
  ProgressDialogHelper (const CStdString& heading) : m_dialog(0)
  {
    if (g_application.IsCurrentThread() && !g_application.IsPlaying())
      m_dialog = (CGUIDialogProgress*) g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);

    if (m_dialog)
    {
      m_dialog->SetHeading (heading); 
      m_dialog->SetLine(0, "");
      m_dialog->SetLine(1, "");
      m_dialog->SetLine(2, "");
      g_application.ProcessSlowEnable(false); // ProcessSlow will normally be done while dialog->Progress and may call back recursively to wake sleeping server !
    }
  }
  ~ProgressDialogHelper ()
  {
    if (m_dialog)
    {
      m_dialog->Close();
      g_application.ProcessSlowEnable(true);
    }
  }

  enum wait_result { TimedOut, Canceled, Success };

  wait_result ShowAndWait (const WaitCondition& waitObj, unsigned timeOutSec, const CStdString& line1)
  {
    unsigned timeOutMs = timeOutSec * 1000;

    if (m_dialog)
    {
      m_dialog->SetLine(1, line1);

      m_dialog->SetPercentage(1); // avoid flickering by starting at 1% ..
    }

    XbmcThreads::EndTime end_time (timeOutMs);

    while (!end_time.IsTimePast())
    {
      if (waitObj.SuccessWaiting())
        return Success;
            
      if (m_dialog)
      {
        if (!m_dialog->IsActive())
          m_dialog->StartModal();

        if (m_dialog->IsCanceled())
          return Canceled;

        m_dialog->Progress();

        unsigned ms_passed = timeOutMs - end_time.MillisLeft();

        int percentage = (ms_passed * 100) / timeOutMs;
        m_dialog->SetPercentage(max(percentage, 1)); // avoid flickering , keep minimum 1%
      }

      Sleep (m_dialog ? 20 : 200);
    }

    return TimedOut;
  }

private:
  CGUIDialogProgress* m_dialog;
};

class NetworkStartWaiter : public WaitCondition
{
public:
  NetworkStartWaiter (unsigned settle_time_ms) : m_settle_time_ms (settle_time_ms)
  {
  }
  virtual bool SuccessWaiting () const
  {
    CNetworkInterface* iface = g_application.getNetwork().GetFirstConnectedInterface();

    bool online = iface && iface->IsEnabled();

    if (!online) // setup endtime so we dont return true until network is consistently connected
      m_end.Set (m_settle_time_ms);

    return online && m_end.IsTimePast();
  }
private:
  mutable XbmcThreads::EndTime m_end;
  unsigned m_settle_time_ms;
};

class PingResponseWaiter : public WaitCondition, private IJobCallback
{
public:
  PingResponseWaiter (const CStdString& host) : m_hostOnline (false)
  {
    m_job = new CHostProberJob(host);
    CSingleLock lock(m_section);
    m_jobId = CJobManager::GetInstance().AddJob(m_job, this);
  }
  ~PingResponseWaiter()
  {
    CSingleLock lock(m_section);
    if (m_job)
      CJobManager::GetInstance().CancelJob(m_jobId);
  }
  virtual bool SuccessWaiting () const
  {
    return m_hostOnline;
  }

  virtual void OnJobComplete(unsigned int jobID, bool success, CJob *job)
  {
    m_hostOnline = success;
    CSingleLock lock(m_section);
    m_job = 0;
  }

private:
  class CHostProberJob : public CJob
  {
    public:
      CHostProberJob(const CStdString& host) : m_host (host) {}

      virtual bool DoWork()
      {
        while (!ShouldCancel(0,0))
        {
          ULONG dst_ip = HostToIP(m_host);

          if (g_application.getNetwork().PingHost(dst_ip, 2000))
            return true;
        }
        return false;
      }

    private:
      CStdString m_host;
  };

  bool m_hostOnline;
  CHostProberJob* m_job;
  unsigned int m_jobId;
  CCriticalSection m_section;
};

//

class NestDetect
{
public:
  NestDetect() : m_gui_thread (g_application.IsCurrentThread())
  {
    if (m_gui_thread)
      ++m_nest;
  }
  ~NestDetect()
  {
    if (m_gui_thread)
      m_nest--;
  }
  bool TooDeep() const
  {
    return m_gui_thread && m_nest > 1;
  }

private:
  static int m_nest;
  const bool m_gui_thread;
};
int NestDetect::m_nest = 0;

//

CWakeOnAccess::CWakeOnAccess()
  : m_netinit_sec(DEFAULT_NETWORK_INIT_SEC)    // wait for network to connect
  , m_netsettle_ms(DEFAULT_NETWORK_SETTLE_MS)  // wait for network to settle
  , m_enabled(false)
{
}

CWakeOnAccess &CWakeOnAccess::Get()
{
  static CWakeOnAccess sWakeOnAccess;
  return sWakeOnAccess;
}

void CWakeOnAccess::WakeUpHost(const CURL& url)
{
  CStdString hostName = url.GetHostName();

  if (!hostName.IsEmpty())
    WakeUpHost (hostName, url.Get());
}

void CWakeOnAccess::WakeUpHost (const CStdString& hostName, const string& customMessage)
{
  if (!IsEnabled())
    return; // bail if feature is turned off

  for (EntriesVector::iterator i = m_entries.begin();i != m_entries.end(); ++i)
  {
    WakeUpEntry& server = *i;

    if (hostName.Equals(server.host.c_str()))
    {
      CDateTime now = CDateTime::GetCurrentDateTime();

      if (now > server.nextWake)
      {
        CLog::Log(LOGINFO,"WakeOnAccess [%s] trigged by accessing : %s", hostName.c_str(), customMessage.c_str());

        NestDetect nesting ; // detect recursive calls on gui thread and bail out

        if (nesting.TooDeep()) // we can not maintain progress-dialog if it gets called back in loop
        {
          CLog::Log(LOGERROR,"WakeOnAccess recursively called on gui-thread : aborting ");
          return;
        }

        WakeUpHost(server);
      }

      server.nextWake = now + server.timeout;

      return;
    }
  }
}

#define LOCALIZED(id) g_localizeStrings.Get(id)

void CWakeOnAccess::WakeUpHost(const WakeUpEntry& server)
{
  CStdString heading = LOCALIZED(13027);
  heading.Format (heading, server.host);

  ProgressDialogHelper dlg (heading);

  {
    NetworkStartWaiter waitObj (m_netsettle_ms); // wait until network connected before sending wake-on-lan

    if (dlg.ShowAndWait (waitObj, m_netinit_sec, LOCALIZED(13030)) != ProgressDialogHelper::Success)
    {
      CLog::Log(LOGINFO,"WakeOnAccess timeout/cancel while waiting for network");
      return; // timedout or canceled
    }
  }

  {
    ULONG dst_ip = HostToIP(server.host);

    if (g_application.getNetwork().PingHost(dst_ip, 500)) // quick ping with short timeout to not block too long
    {
      CLog::Log(LOGINFO,"WakeOnAccess success exit, server already running");

      if (!g_application.IsPlaying())
        CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, heading, LOCALIZED(13028), 1000);
      return;
    }
  }

  if (!g_application.getNetwork().WakeOnLan(server.mac.c_str()))
  {
    CLog::Log(LOGERROR,"WakeOnAccess failed to send. (Is it blocked by firewall?)");

    if (!g_application.IsPlaying())
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, heading, LOCALIZED(13029));
    return;
  }

  {
    PingResponseWaiter waitObj (server.host); // wait for ping response ..

    ProgressDialogHelper::wait_result 
      result = dlg.ShowAndWait (waitObj, server.wait_online1_sec, LOCALIZED(13031));

    if (result == ProgressDialogHelper::TimedOut)
      result = dlg.ShowAndWait (waitObj, server.wait_online2_sec, LOCALIZED(13032));

    if (result != ProgressDialogHelper::Success)
    {
      CLog::Log(LOGINFO,"WakeOnAccess timeout/cancel while waiting for response");
      return; // timedout or canceled
    }
  }

  {
    WaitCondition waitObj ; // wait uninteruptable fixed time for services ..

    dlg.ShowAndWait (waitObj, server.wait_services_sec, LOCALIZED(13033));

    CLog::Log(LOGINFO,"WakeOnAccess sequence completed, server started");
  }
}

static void AddHost (const CStdString& host, vector<string>& hosts)
{
  for (vector<string>::const_iterator it = hosts.begin(); it != hosts.end(); ++it)
    if (host.Equals((*it).c_str()))
      return; // allready there ..

  if (!host.IsEmpty())
    hosts.push_back(host);
}

static void AddHostFromDatabase(const DatabaseSettings& setting, vector<string>& hosts)
{
  if (setting.type.Equals("mysql"))
    AddHost(setting.host, hosts);
}

void CWakeOnAccess::QueueMACDiscoveryForHost(const CStdString& host)
{
  if (IsEnabled())
    CJobManager::GetInstance().AddJob(new CMACDiscoveryJob(host), this);
}

void CWakeOnAccess::AddHostsFromShare(const CMediaSource& source, std::vector<std::string>& hosts)
{
  for (CStdStringArray::const_iterator it = source.vecPaths.begin() ; it != source.vecPaths.end(); it++)
  {
    CURL url = *it;

    AddHost (url.GetHostName(), hosts);
  }
}

void CWakeOnAccess::AddHostsFromShares(const VECSOURCES& sources, vector<string>& hosts)
{
  for (VECSOURCES::const_iterator it = sources.begin(); it != sources.end(); it++)
    AddHostsFromShare(*it, hosts);
}

void CWakeOnAccess::QueueMACDiscoveryForShare(const CMediaSource& source)
{
  vector<string> hosts;
  AddHostsFromShare(source, hosts);
  for (vector<string>::const_iterator it = hosts.begin(); it != hosts.end(); it++)
    QueueMACDiscoveryForHost(*it);
}

void CWakeOnAccess::QueueMACDiscoveryForAllRemotes()
{
  vector<string> hosts;

  // add media sources
  AddHostsFromShares(g_settings.m_videoSources, hosts);
  AddHostsFromShares(g_settings.m_musicSources, hosts);
  AddHostsFromShares(g_settings.m_pictureSources, hosts);
  AddHostsFromShares(g_settings.m_fileSources, hosts);

  // add mysql servers
  AddHostFromDatabase(g_advancedSettings.m_databaseVideo, hosts);
  AddHostFromDatabase(g_advancedSettings.m_databaseMusic, hosts);
  AddHostFromDatabase(g_advancedSettings.m_databaseEpg, hosts);
  AddHostFromDatabase(g_advancedSettings.m_databaseTV, hosts);

  // add from path substitutions ..
  for (CAdvancedSettings::StringMapping::iterator i = g_advancedSettings.m_pathSubstitutions.begin(); i != g_advancedSettings.m_pathSubstitutions.end(); ++i)
  {
    CURL url = i->second;

    AddHost (url.GetHostName(), hosts);
  }

  for (vector<string>::const_iterator it = hosts.begin(); it != hosts.end(); it++)
    QueueMACDiscoveryForHost(*it);
}

void CWakeOnAccess::OnJobComplete(unsigned int jobID, bool success, CJob *job)
{
  CMACDiscoveryJob* discoverJob = (CMACDiscoveryJob*)job;

  CStdString heading = LOCALIZED(13034);

  if (success)
  {
    CLog::Log(LOGINFO, "%s - Mac for host '%s' - '%s'", __FUNCTION__, discoverJob->GetHost().c_str(), discoverJob->GetMAC().c_str());
    for (EntriesVector::iterator i = m_entries.begin(); i != m_entries.end(); ++i)
    {
      if (discoverJob->GetHost().Equals(i->host.c_str()))
      {
        CLog::Log(LOGDEBUG, "%s - Update existing entry for host '%s'", __FUNCTION__, discoverJob->GetHost().c_str());
        if (!discoverJob->GetMAC().Equals(i->mac.c_str()))
        {
          if (IsEnabled()) // show notification only if we have general feature enabled
          {
            CStdString message = LOCALIZED(13035);
            message.Format(message, discoverJob->GetHost());
            CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, heading, message, 4000, true, 3000);
          }

          i->mac = discoverJob->GetMAC();
          SaveToXML();
        }

        return;
      }
    }

    // not found entry to update - create using default values
    WakeUpEntry entry (true);
    entry.host = discoverJob->GetHost();
    entry.mac  = discoverJob->GetMAC();
    m_entries.push_back(entry);

    CLog::Log(LOGDEBUG, "%s - Create new entry for host '%s'", __FUNCTION__, discoverJob->GetHost().c_str());
    if (IsEnabled()) // show notification only if we have general feature enabled
    {
      CStdString message = LOCALIZED(13036);
      message.Format(message, discoverJob->GetHost());
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, heading, message, 4000, true, 3000);
    }

    SaveToXML();
  }
  else if (IsEnabled())
  {
    CStdString message = LOCALIZED(13037);
    message.Format(message, discoverJob->GetHost());
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, heading, message, 4000, true, 3000);
  }
}

CStdString CWakeOnAccess::GetSettingFile()
{
  return CSpecialProtocol::TranslatePath("special://masterprofile/wakeonlan.xml");
}

void CWakeOnAccess::LoadFromXML()
{
  SetEnabled(g_guiSettings.GetBool("powermanagement.wakeonaccess"));

  CXBMCTinyXML xmlDoc;
  if (!xmlDoc.LoadFile(GetSettingFile()))
  {
    CLog::Log(LOGINFO, "%s - unable to load:%s", __FUNCTION__, GetSettingFile().c_str());
    return;
  }

  TiXmlElement* pRootElement = xmlDoc.RootElement();
  if (strcmpi(pRootElement->Value(), "onaccesswakeup"))
  {
    CLog::Log(LOGERROR, "%s - XML file %s doesnt contain <onaccesswakeup>", __FUNCTION__, GetSettingFile().c_str());
    return;
  }

  m_entries.clear();

  CLog::Log(LOGINFO,"WakeOnAccess - Load settings :");

  int tmp;
  if (XMLUtils::GetInt(pRootElement, "netinittimeout", tmp, 0, 5 * 60))
    m_netinit_sec = tmp;
  CLog::Log(LOGINFO,"  -Network init timeout : [%d] sec", m_netinit_sec);
  
  if (XMLUtils::GetInt(pRootElement, "netsettletime", tmp, 0, 5 * 1000))
    m_netsettle_ms = tmp;
  CLog::Log(LOGINFO,"  -Network settle time  : [%d] ms", m_netsettle_ms);

  const TiXmlNode* pWakeUp = pRootElement->FirstChildElement("wakeup");
  while (pWakeUp)
  {
    WakeUpEntry entry;

    CStdString strtmp;
    if (XMLUtils::GetString(pWakeUp, "host", strtmp))
      entry.host = strtmp;

    if (XMLUtils::GetString(pWakeUp, "mac", strtmp))
      entry.mac = strtmp;

    if (entry.host.empty())
      CLog::Log(LOGERROR, "%s - Missing <host> tag or it's empty", __FUNCTION__);
    else if (entry.mac.empty())
       CLog::Log(LOGERROR, "%s - Missing <mac> tag or it's empty", __FUNCTION__);
    else
    {
      if (XMLUtils::GetInt(pWakeUp, "timeout", tmp, 10, 12 * 60 * 60))
        entry.timeout.SetDateTimeSpan (0, 0, 0, tmp);

      if (XMLUtils::GetInt(pWakeUp, "waitonline", tmp, 0, 10 * 60)) // max 10 minutes
        entry.wait_online1_sec = tmp;

      if (XMLUtils::GetInt(pWakeUp, "waitonline2", tmp, 0, 10 * 60)) // max 10 minutes
        entry.wait_online2_sec = tmp;

      if (XMLUtils::GetInt(pWakeUp, "waitservices", tmp, 0, 5 * 60)) // max 5 minutes
        entry.wait_services_sec = tmp;

      CLog::Log(LOGINFO,"  Registering wakeup entry:");
      CLog::Log(LOGINFO,"    HostName        : %s", entry.host.c_str());
      CLog::Log(LOGINFO,"    MacAddress      : %s", entry.mac.c_str());
      CLog::Log(LOGINFO,"    Timeout         : %d (sec)", GetTotalSeconds(entry.timeout));
      CLog::Log(LOGINFO,"    WaitForOnline   : %d (sec)", entry.wait_online1_sec);
      CLog::Log(LOGINFO,"    WaitForOnlineEx : %d (sec)", entry.wait_online2_sec);
      CLog::Log(LOGINFO,"    WaitForServices : %d (sec)", entry.wait_services_sec);

      m_entries.push_back(entry);
    }

    pWakeUp = pWakeUp->NextSiblingElement("wakeup"); // get next one
  }
}

void CWakeOnAccess::SaveToXML()
{
  CXBMCTinyXML xmlDoc;
  TiXmlElement xmlRootElement("onaccesswakeup");
  TiXmlNode *pRoot = xmlDoc.InsertEndChild(xmlRootElement);
  if (!pRoot) return;

  XMLUtils::SetInt(pRoot, "netinittimeout", m_netinit_sec);
  XMLUtils::SetInt(pRoot, "netsettletime", m_netsettle_ms);

  for (EntriesVector::const_iterator i = m_entries.begin(); i != m_entries.end(); ++i)
  {
    TiXmlElement xmlSetting("wakeup");
    TiXmlNode* pWakeUpNode = pRoot->InsertEndChild(xmlSetting);
    if (pWakeUpNode)
    {
      XMLUtils::SetString(pWakeUpNode, "host", i->host);
      XMLUtils::SetString(pWakeUpNode, "mac", i->mac);
      XMLUtils::SetInt(pWakeUpNode, "timeout", GetTotalSeconds(i->timeout));
      XMLUtils::SetInt(pWakeUpNode, "waitonline", i->wait_online1_sec);
      XMLUtils::SetInt(pWakeUpNode, "waitonline2", i->wait_online2_sec);
      XMLUtils::SetInt(pWakeUpNode, "waitservices", i->wait_services_sec);
    }
  }

  xmlDoc.SaveFile(GetSettingFile());
}
