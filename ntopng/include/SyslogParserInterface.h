/*
 *
 * (C) 2019 - ntop.org
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

#ifndef _SYSLOG_PARSER_INTERFACE_H_
#define _SYSLOG_PARSER_INTERFACE_H_

#include "ntop_includes.h"

class SyslogParserInterface : public ParserInterface {
 private:

  void parseSuricataFlow(json_object *f, ParsedFlow *flow);
  void parseSuricataNetflow(json_object *f, ParsedFlow *flow);
  void parseSuricataAlert(json_object *a, ParsedFlow *flow, ICMPinfo *icmp_info, bool flow_alert);

 public:
  SyslogParserInterface(const char *endpoint, const char *custom_interface_type = NULL);
  ~SyslogParserInterface();

  u_int8_t parseLog(char *log_line);

  u_int32_t getNumDroppedPackets() { return 0; };
  virtual void lua(lua_State* vm);
};

#endif /* _SYSLOG_PARSER_INTERFACE_H_ */


