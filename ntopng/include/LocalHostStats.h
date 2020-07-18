/*
 *
 * (C) 2013-19 - ntop.org
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef _LOCAL_HOST_STATS_H_
#define _LOCAL_HOST_STATS_H_

class LocalHostStats: public HostStats {
 protected:
  /* Written by NetworkInterface::processPacket thread */
  DnsStats *dns;
  HTTPstats *http;
  ICMPstats *icmp;
  FrequentStringItems *top_sites;
  time_t nextSitesUpdate;
  map<Host*, u_int16_t> contacts_as_cli, contacts_as_srv;

  /* Written by NetworkInterface::periodicStatsUpdate thread */
  char *old_sites;
  TimeseriesRing *ts_ring;

 public:
  LocalHostStats(Host *_host);
  virtual ~LocalHostStats();

  virtual void incStats(time_t when, u_int8_t l4_proto, u_int ndpi_proto,
		custom_app_t custom_app,
		u_int64_t sent_packets, u_int64_t sent_bytes, u_int64_t sent_goodput_bytes,
		u_int64_t rcvd_packets, u_int64_t rcvd_bytes, u_int64_t rcvd_goodput_bytes,
		bool peer_is_unicast);
  virtual void updateStats(struct timeval *tv);
  virtual void getJSONObject(json_object *my_object, DetailsLevel details_level);
  virtual void deserialize(json_object *obj);
  virtual void lua(lua_State* vm, bool mask_host, DetailsLevel details_level, bool tsLua = false);
  virtual void incNumFlows(bool as_client, Host *peer);
  virtual void decNumFlows(bool as_client, Host *peer);

  virtual void incICMP(u_int8_t icmp_type, u_int8_t icmp_code, bool sent, Host *peer);
  virtual void incNumDNSQueriesSent(u_int16_t query_type) { if(dns) dns->incNumDNSQueriesSent(query_type); };
  virtual void incNumDNSQueriesRcvd(u_int16_t query_type) { if(dns) dns->incNumDNSQueriesRcvd(query_type); };
  virtual void incNumDNSResponsesSent(u_int32_t ret_code) { if(dns) dns->incNumDNSResponsesSent(ret_code); };
  virtual void incNumDNSResponsesRcvd(u_int32_t ret_code) { if(dns) dns->incNumDNSResponsesRcvd(ret_code); };
  virtual void luaDNS(lua_State *vm, bool verbose) const  { if(dns) dns->lua(vm,verbose); }
  virtual void luaICMP(lua_State *vm, bool isV4, bool verbose) const    { if (icmp) icmp->lua(isV4, vm, verbose); }
  virtual void incrVisitedWebSite(char *hostname);
  virtual void tsLua(lua_State* vm);
  virtual bool hasAnomalies(time_t when);
  virtual void luaAnomalies(lua_State* vm, time_t when);
  virtual HTTPstats* getHTTPstats() { return(http); };
  virtual u_int16_t getNumActiveContactsAsClient() { return contacts_as_cli.size(); }
  virtual u_int16_t getNumActiveContactsAsServer() { return contacts_as_srv.size(); }
};

#endif
