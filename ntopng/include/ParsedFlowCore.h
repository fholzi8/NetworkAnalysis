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

#ifndef _PARSED_FLOW_CORE_H_
#define _PARSED_FLOW_CORE_H_

#include "ntop_includes.h"

class ParsedFlowCore {
 public:
  u_int8_t src_mac[6], dst_mac[6], direction, source_id;
  IpAddress src_ip, dst_ip;
  u_int32_t first_switched, last_switched;
  u_int8_t version; /* 0 so far */
  u_int32_t deviceIP;
  u_int16_t src_port, dst_port, inIndex, outIndex;
  ndpi_proto l7_proto;
  u_int16_t vlan_id, pkt_sampling_rate;
  u_int8_t l4_proto;
  u_int32_t in_pkts, in_bytes, out_pkts, out_bytes, vrfId;
  u_int32_t in_fragments, out_fragments;
  u_int8_t absolute_packet_octet_counters;
  struct {
    u_int8_t tcp_flags, client_tcp_flags, server_tcp_flags;
    u_int32_t ooo_in_pkts, ooo_out_pkts;
    u_int32_t retr_in_pkts, retr_out_pkts;
    u_int32_t lost_in_pkts, lost_out_pkts;
    struct timeval clientNwLatency, serverNwLatency;
    float applLatencyMsec;
  } tcp;

  ParsedFlowCore();
  ParsedFlowCore(const ParsedFlowCore &pfc);
  virtual ~ParsedFlowCore();
  void swap();


  void print();
};

#endif /* _PARSED_FLOW_CORE_H_ */
