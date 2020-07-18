--
-- (C) 2018 - ntop.org
--

require "lua_utils"
local json = require "dkjson"

local syslog = {}

syslog.DEFAULT_SEVERITY = "info"
syslog.EXPORT_FREQUENCY = 1 -- 1 second, i.e., as soon as possible

function syslog.dequeueAlerts(queue)
   local notifications = ntop.lrangeCache(queue, 0, -1)

   if not notifications then
      return {success = true}
   end

   local syslog_format = ntop.getPref("ntopng.prefs.syslog_alert_format")
   if isEmptyString(syslog_format) then
      syslog_format = "plaintext"
   end

   -- Separate by severity and channel
   local alerts_by_types = {}

   for _, json_message in ipairs(notifications) do
     local notif = alertNotificationToObject(json_message)

      alerts_by_types[notif.entity_type] = alerts_by_types[notif.entity_type] or {}
      alerts_by_types[notif.entity_type][notif.severity] = alerts_by_types[notif.entity_type][notif.severity] or {}
      table.insert(alerts_by_types[notif.entity_type][notif.severity], notif)
   end

   for entity_type, by_severity in pairs(alerts_by_types) do
      for severity, notifications in pairs(by_severity) do
	 -- Most recent notifications first
	 for _, notif in pairsByValues(notifications, notification_timestamp_rev) do
	    local syslog_severity = alertLevelToSyslogLevel(notif.severity)

	    local msg

	    if syslog_format == "plaintext" then
	       -- prepare a plaintext message
	       msg = formatAlertNotification(notif, {nohtml = true,
						     show_severity = true,
						     show_entity = true})
	    else -- syslog_format == "json" then
	       -- send out the json message but prepare a nice
	       -- message
	       notif.message  = formatAlertNotification(notif, {nohtml = true,
								show_severity = false,
								show_entity = false})
	       msg = json.encode(notif)
	    end

	    ntop.syslog(msg, syslog_severity)
	 end
      end
   end

   -- Remove all the messages from queue on success
   ntop.delCache(queue)

   return {success = true}
end

return syslog
