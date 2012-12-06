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

#include "URL.h"
#include "MediaSource.h"
#include "XBDateTime.h"
#include "utils/Job.h"

class CWakeOnAccess : public IJobCallback
{
public:
  CWakeOnAccess();
  static CWakeOnAccess &Get();

  void WakeUpHost (const CURL& fileUrl);
  void WakeUpHost (const CStdString& hostName, const std::string& customMessage);

  void SetEnabled(bool enabled) { m_enabled = enabled; }
  bool IsEnabled() const { return m_enabled; }

  void QueueMACDiscoveryForHost(const CStdString& host);
  void QueueMACDiscoveryForShare(const CMediaSource& source);
  void QueueMACDiscoveryForAllRemotes();

  virtual void OnJobComplete(unsigned int jobID, bool success, CJob *job);

  void LoadFromXML();
private:
  void AddHostsFromShare(const CMediaSource& source, std::vector<std::string>& hosts);
  void AddHostsFromShares(const VECSOURCES& sources, std::vector<std::string>& hosts);

  CStdString GetSettingFile();
  void SaveToXML();

  class CMACDiscoveryJob : public CJob
  {
  public:
    CMACDiscoveryJob(const CStdString& host);

    virtual bool DoWork();

    const CStdString& GetMAC() const;
    const CStdString& GetHost() const;
  private:
    CStdString m_macAddres;
    CStdString m_host;
  };
  
  // struct to keep per host settings
  struct WakeUpEntry
  {
    WakeUpEntry (bool isAwake = false);

    std::string host;
    std::string mac;
    CDateTimeSpan timeout;
    unsigned int wait_online1_sec; // initial wait
    unsigned int wait_online2_sec; // extended wait
    unsigned int wait_services_sec;

    CDateTime nextWake;
  };

  typedef std::vector<WakeUpEntry> EntriesVector;
  EntriesVector m_entries;

  unsigned int m_netinit_sec, m_netsettle_ms; //time to wait for network connection

  bool m_enabled;

  void WakeUpHost(const WakeUpEntry& server);
};
