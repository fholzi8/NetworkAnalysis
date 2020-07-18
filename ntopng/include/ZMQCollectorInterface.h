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

#ifndef _ZMQ_COLLECTOR_INTERFACE_H_
#define _ZMQ_COLLECTOR_INTERFACE_H_

#include "ntop_includes.h"

#ifndef HAVE_NEDGE

class LuaEngine;

typedef struct {
  char *endpoint;
  void *socket;
} zmq_subscriber;

class ZMQCollectorInterface : public ZMQParserInterface {
 private:
  void *context;
  struct {
    u_int32_t num_flows, num_events, num_counters,
      num_templates, num_options, num_network_events,
      zmq_msg_drops;
  } recvStats;
  std::map<u_int8_t, u_int32_t>source_id_last_msg_id;
  bool is_collector;
  u_int8_t num_subscribers;
  zmq_subscriber subscriber[MAX_ZMQ_SUBSCRIBERS];

 public:
  ZMQCollectorInterface(const char *_endpoint);
  ~ZMQCollectorInterface();

  inline const char* get_type()           { return(CONST_INTERFACE_TYPE_ZMQ);      };
  virtual InterfaceType getIfType() const { return(interface_type_ZMQ); }
  virtual bool is_ndpi_enabled() const    { return(false);      };
  inline char* getEndpoint(u_int8_t id)   { return((id < num_subscribers) ?
						   subscriber[id].endpoint : (char*)""); };
  virtual bool isPacketInterface() const  { return(false);      };
  void collect_flows();

  virtual void purgeIdle(time_t when);

  void startPacketPolling();
  void shutdown();
  bool set_packet_filter(char *filter);
  virtual void lua(lua_State* vm);
};

#endif /* HAVE_NEDGE */

#endif /* _ZMQ_COLLECTOR_INTERFACE_H_ */

