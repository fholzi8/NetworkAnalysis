--
-- (C) 2018 - ntop.org
--

local driver = {}

local os_utils = require("os_utils")
local ts_common = require("ts_common")
require("ntop_utils")
require("rrd_paths")

local RRD_CONSOLIDATION_FUNCTION = "AVERAGE"
local use_hwpredict = false

local type_to_rrdtype = {
  [ts_common.metrics.counter] = "DERIVE",
  [ts_common.metrics.gauge] = "GAUGE",
}

-- ##############################################

local debug_enabled = nil
local function isDebugEnabled()
  if debug_enabled == nil then
    -- cache it
    debug_enabled = (ntop.getPref("ntopng.prefs.rrd_debug_enabled") == "1")
  end

  return(debug_enabled)
end

-- ##############################################

function driver:new(options)
  local obj = {
    base_path = options.base_path,
  }

  setmetatable(obj, self)
  self.__index = self

  return obj
end

-- ##############################################

function driver:export()
  return
end

-- ##############################################

function driver:getLatestTimestamp(ifid)
  return os.time()
end

-- ##############################################

-- TODO remove after migrating to the new path format
-- Maps second tag name to getRRDName
local HOST_PREFIX_MAP = {
  host = "",
  mac = "",
  subnet = "net:",
  flowdev_port = "flow_device:",
  sflowdev_port = "sflow:",
  snmp_if = "snmp:",
  host_pool = "pool:",
}
local WILDCARD_TAGS = {protocol=1, category=1, l4proto=1}

local function get_fname_for_schema(schema, tags)
  if schema.options.rrd_fname ~= nil then
    return schema.options.rrd_fname
  end

  local last_tag = schema._tags[#schema._tags]

  if WILDCARD_TAGS[last_tag] then
    -- return the last defined tag
    return tags[last_tag]
  end

  -- e.g. host:contacts -> contacts
  local suffix = string.split(schema.name, ":")[2]
  return suffix
end

local function schema_get_path(schema, tags)
  local parts = {schema.name, }
  local suffix = ""
  local rrd

  -- ifid is mandatory here
  local ifid = tags.ifid or -1
  local host_or_network = nil
  local parts = string.split(schema.name, ":")

  if((string.find(schema.name, "iface:") ~= 1) and  -- interfaces are only identified by the first tag
      (#schema._tags >= 2)) then                    -- some schema do not have 2 tags, e.g. "process:*" schemas
    host_or_network = (HOST_PREFIX_MAP[parts[1]] or (parts[1] .. ":")) .. tags[schema._tags[2]]
  end

  -- Some exceptions to avoid conflicts / keep compatibility
  if parts[1] == "snmp_if" then
     suffix = tags.if_index .. "/"
  elseif (parts[1] == "flowdev_port") or (parts[1] == "sflowdev_port") then
     suffix = tags.port .. "/"
  elseif parts[2] == "ndpi_categories" then
     suffix = "ndpi_categories/"
  end

  local path = getRRDName(ifid, host_or_network) .. suffix
  local rrd = get_fname_for_schema(schema, tags)

  return path, rrd
end

function driver.schema_get_full_path(schema, tags)
  local base, rrd = schema_get_path(schema, tags)
  local full_path = os_utils.fixPath(base .. "/" .. rrd .. ".rrd")

  return full_path
end

-- TODO remove after migration
function find_schema(rrdFile, rrdfname, tags, ts_utils)
  -- try to guess additional tags
  local v = string.split(rrdfname, "%.rrd")
  if((v ~= nil) and (#v == 1)) then
    local app = v[1]

    if interface.getnDPIProtoId(app) ~= -1 then
      tags.protocol = app
    elseif interface.getnDPICategoryId(app) ~= -1 then
      tags.category = app
    end
  end

  for schema_name, schema in pairs(ts_utils.getLoadedSchemas()) do
    -- verify tags compatibility
    for tag in pairs(schema.tags) do
      if tags[tag] == nil then
        goto next_schema
      end
    end

    local full_path = driver.schema_get_full_path(schema, tags)

    if full_path == rrdFile then
      return schema_name
    end

    ::next_schema::
  end

  return nil
end

-- ##############################################

local function getRRAParameters(step, resolution, retention_time)
  local aggregation_dp = math.ceil(resolution / step)
  local retention_dp = math.ceil(retention_time / resolution)
  return aggregation_dp, retention_dp
end

-- This is necessary to keep the current RRD format
local function map_metrics_to_rrd_columns(num)
  if num == 1 then
    return {"num"}
  elseif num == 2 then
    return {"sent", "rcvd"}
  elseif num == 3 then
    return {"ingress", "egress", "inner"}
  end

  -- error
  return nil
end

-- This is necessary to keep the current RRD format
local function map_rrd_column_to_metrics(schema, column_name)
   if (column_name == "num") or starts(column_name, "num_")
      or (column_name == "drops") or starts(column_name, "tcp_")
      or (column_name == "sent") or (column_name == "ingress")
      or (column_name == "bytes") or (column_name == "packets")
   then
    return 1
  elseif (column_name == "rcvd") or (column_name == "egress") then
    return 2
  elseif (column_name == "inner") then
    return 3
  end

  traceError(TRACE_ERROR, TRACE_CONSOLE, "unknown column name (" .. column_name .. ") in schema " .. schema.name)
  return nil
end

local function create_rrd(schema, path)
  local heartbeat = schema.options.rrd_heartbeat or (schema.options.step * 2)
  local rrd_type = type_to_rrdtype[schema.options.metrics_type]
  local params = {path, schema.options.step}

  local metrics_map = map_metrics_to_rrd_columns(#schema._metrics)
  if not metrics_map then
    traceError(TRACE_ERROR, TRACE_CONSOLE, "unsupported number of metrics (" .. (#schema._metrics) .. ") in schema " .. schema.name)
    return false
  end

  for idx, metric in ipairs(schema._metrics) do
    params[#params + 1] = "DS:" .. metrics_map[idx] .. ":" .. rrd_type .. ':' .. heartbeat .. ':U:U'
  end

  for _, rra in ipairs(schema.retention) do
    params[#params + 1] = "RRA:" .. RRD_CONSOLIDATION_FUNCTION .. ":0.5:" .. rra.aggregation_dp .. ":" .. rra.retention_dp
  end

  if use_hwpredict and schema.hwpredict then
    -- NOTE: at most one RRA, otherwise rrd_update crashes.
    local hwpredict = schema.hwpredict
    params[#params + 1] = "RRA:HWPREDICT:" .. hwpredict.row_count .. ":0.1:0.0035:" .. hwpredict.period
  end

  if isDebugEnabled() then
    traceError(TRACE_NORMAL, TRACE_CONSOLE, string.format("ntop.rrd_create(%s) schema=%s",
      table.concat(params, ", "), schema.name))
  end

  local err = ntop.rrd_create(table.unpack(params))
  if(err ~= nil) then
    traceError(TRACE_ERROR, TRACE_CONSOLE, err)
    return false
  end

  return true
end

-- ##############################################

local function handle_old_rrd_tune(schema, rrdfile)
  -- In this case the only thing we can do is to remove the file and create a new one
  if ntop.getCache("ntopng.cache.rrd_format_change_warning_shown") ~= "1" then
    traceError(TRACE_WARNING, TRACE_CONSOLE, "RRD format change detected, incompatible RRDs will be moved to '.rrd.bak' files")
    ntop.setCache("ntopng.cache.rrd_format_change_warning_shown", "1")
  end

  os.rename(rrdfile, rrdfile .. ".bak")

  if(not create_rrd(schema, rrdfile)) then
    return false
  end

  return true
end

local function add_missing_ds(schema, rrdfile, cur_ds)
  local cur_metrics = map_metrics_to_rrd_columns(cur_ds)
  local new_metrics = map_metrics_to_rrd_columns(#schema._metrics)

  if((cur_metrics == nil) or (new_metrics == nil)) then
    return false
  end

  if cur_ds >= #new_metrics then
    return false
  end

  traceError(TRACE_INFO, TRACE_CONSOLE, "RRD format changed [schema=".. schema.name .."], trying to fix " .. rrdfile)

  local params = {rrdfile, }
  local heartbeat = schema.options.rrd_heartbeat or (schema.options.step * 2)
  local rrd_type = type_to_rrdtype[schema.options.metrics_type]

  for idx, metric in ipairs(schema._metrics) do
    local old_name = cur_metrics[idx]
    local new_name = new_metrics[idx]

    if old_name == nil then
      params[#params + 1] = "DS:" .. new_name .. ":" .. rrd_type .. ':' .. heartbeat .. ':U:U'
    elseif old_name ~= new_name then
      params[#params + 1] = "--data-source-rename"
      params[#params + 1] = old_name ..":" .. new_name
    end
  end

  local err = ntop.rrd_tune(table.unpack(params))
  if(err ~= nil) then
    if(string.find(err, "unknown data source name") ~= nil) then
      -- the RRD was already mangled by incompatible rrd_tune
      return handle_old_rrd_tune(schema, rrdfile)
    else
      traceError(TRACE_ERROR, TRACE_CONSOLE, err)
      return false
    end
  end

  -- Double check as some older implementations do not support adding a column and will silently fail
  local last_update, num_ds = ntop.rrd_lastupdate(rrdfile)
  if num_ds ~= #new_metrics then
    return handle_old_rrd_tune(schema, rrdfile)
  end

  traceError(TRACE_INFO, TRACE_CONSOLE, "RRD successfully fixed: " .. rrdfile)
  return true
end

-- ##############################################

local function update_rrd(schema, rrdfile, timestamp, data, dont_recover)
  local params = {tolongint(timestamp), }

  for _, metric in ipairs(schema._metrics) do
    params[#params + 1] = tolongint(data[metric])
  end

  if isDebugEnabled() then
    traceError(TRACE_NORMAL, TRACE_CONSOLE, string.format("ntop.rrd_update(%s, %s) schema=%s",
      rrdfile, table.concat(params, ", "), schema.name))
  end

  local err = ntop.rrd_update(rrdfile, table.unpack(params))
  if(err ~= nil) then
    if(dont_recover ~= true) then
      -- Try to recover
      local last_update, num_ds = ntop.rrd_lastupdate(rrdfile)
      local retry = false

      if((num_ds ~= nil) and (num_ds < #schema._metrics)) then
        if add_missing_ds(schema, rrdfile, num_ds) then
          retry = true
        end
      elseif((num_ds == 2) and (schema.name == "iface:traffic")) then
        -- The RRD is corrupted due to collision between "iface:traffic" and "evexporter_iface:traffic"
        traceError(TRACE_WARNING, TRACE_CONSOLE, "'evexporter_iface:traffic' schema collision detected on " .. rrdfile .. ", moving RRD to .old")
        local rv, errmsg = os.rename(rrdfile, rrdfile..".old")

        if(rv == nil) then
          traceError(TRACE_ERROR, TRACE_CONSOLE, errmsg)
          return false
        end

        if(not create_rrd(schema, rrdfile)) then
          return false
        end

        retry = true
      end

      if retry then
        -- Problem possibly fixed, retry
        return update_rrd(schema, rrdfile, timestamp, data, true --[[ do not recovery again ]])
      end
    end

    traceError(TRACE_ERROR, TRACE_CONSOLE, err)
    return false
  end

  return true
end

-- ##############################################

function driver:append(schema, timestamp, tags, metrics)
  local base, rrd = schema_get_path(schema, tags)
  local rrdfile = os_utils.fixPath(base .. "/" .. rrd .. ".rrd")

  if not ntop.exists(rrdfile) then
    ntop.mkdir(base)
    if not create_rrd(schema, rrdfile) then
      return false
    end
  end

  return update_rrd(schema, rrdfile, timestamp, metrics)
end

-- ##############################################

local function makeTotalSerie(series, count)
  local total = {}

  for i=1,count do
    total[i] = 0
  end

  for _, serie in pairs(series) do
    for i, val in pairs(serie.data) do
      total[i] = total[i] + val
    end
  end

  return total
end

-- ##############################################

local function sampleSeries(schema, cur_points, step, max_points, series)
  local sampled_dp = math.ceil(cur_points / max_points)
  local count = nil

  for _, data_serie in pairs(series) do
    local serie = data_serie.data
    local num = 0
    local sum = 0
    local end_idx = 1

    for _, dp in ipairs(serie) do
      sum = sum + dp
      num = num + 1

      if num == sampled_dp then
        -- A data group is ready
        serie[end_idx] = sum / num
        end_idx = end_idx + 1

        num = 0
        sum = 0
      end
    end

    -- Last group
    if num > 0 then
      serie[end_idx] = sum / num
      end_idx = end_idx + 1
    end

    count = end_idx-1

    -- remove the exceeding points
    for i = end_idx, #serie do
      serie[i] = nil
    end
  end

  -- new step, new count, new data
  return step * sampled_dp, count
end

-- ##############################################

-- Make sure we do not fetch data from RRDs that have been update too much long ago
-- as this creates issues with the consolidation functions when we want to compare
-- results coming from different RRDs.
-- This is also needed to make sure that multiple data series on graphs have the
-- same number of points, otherwise d3js will generate errors.
local function touchRRD(rrdname)
  local now  = os.time()
  local last, ds_count = ntop.rrd_lastupdate(rrdname)

  if((last ~= nil) and ((now-last) > 3600)) then
    local tdiff = now - 1800 -- This avoids to set the update continuously

    if isDebugEnabled() then
      traceError(TRACE_NORMAL, TRACE_CONSOLE, string.format("touchRRD(%s, %u), last_update was %u",
        rrdname, tdiff, last))
    end

    if(ds_count == 1) then
      ntop.rrd_update(rrdname, tdiff.."", "0")
    elseif(ds_count == 2) then
      ntop.rrd_update(rrdname, tdiff.."", "0", "0")
    elseif(ds_count == 3) then
      ntop.rrd_update(rrdname, tdiff.."", "0", "0", "0")
    end
  end
end

-- ##############################################

function driver:query(schema, tstart, tend, tags, options)
  local base, rrd = schema_get_path(schema, tags)
  local rrdfile = os_utils.fixPath(base .. "/" .. rrd .. ".rrd")

  if not ntop.exists(rrdfile) then
     return nil
  end

  if isDebugEnabled() then
    traceError(TRACE_NORMAL, TRACE_CONSOLE, string.format("RRD_FETCH schema=%s %s -> (%s): last_update=%u",
      schema.name, table.tconcat(tags, "=", ","), rrdfile, ntop.rrd_lastupdate(rrdfile)))
  end

  touchRRD(rrdfile)

  --tprint("rrdtool fetch ".. rrdfile.. " " .. RRD_CONSOLIDATION_FUNCTION .. " -s ".. tstart .. " -e " .. tend)
  local fstart, fstep, fdata, fend, fcount = ntop.rrd_fetch_columns(rrdfile, RRD_CONSOLIDATION_FUNCTION, tstart, tend)

  if fdata == nil then
    return nil
  end

  local count = 0
  local series = {}

  for name_key, serie in pairs(fdata) do
    local serie_idx = map_rrd_column_to_metrics(schema, name_key)
    local name = schema._metrics[serie_idx]
    local max_val = ts_common.getMaxPointValue(schema, name, tags)
    count = 0

    -- unify the format
    for i, v in pairs(serie) do
      local v = ts_common.normalizeVal(v, max_val, options)
      serie[i] = v
      count = count + 1
    end

    -- Remove the last value: RRD seems to give an additional point
    serie[#serie] = nil
    count = count - 1

    series[serie_idx] = {label=name, data=serie}

    serie_idx = serie_idx + 1
  end

  local unsampled_series = table.clone(series)
  local unsampled_count = count
  local unsampled_fstep = fstep

  if count > options.max_num_points then
    fstep, count = sampleSeries(schema, count, fstep, options.max_num_points, series)
  end

  --local returned_tend = fstart + fstep * (count-1)
  --tprint(returned_tend .. " " .. fstart)

  local total_serie = nil
  local stats = nil

  if options.calculate_stats then
    total_serie = makeTotalSerie(series, count)
    stats = ts_common.calculateStatistics(makeTotalSerie(unsampled_series, unsampled_count), unsampled_fstep, tend - tstart, schema.options.metrics_type)
  end

  if options.initial_point then
    local _, _, initial_pt = ntop.rrd_fetch_columns(rrdfile, RRD_CONSOLIDATION_FUNCTION, tstart-schema.options.step, tstart-schema.options.step)
    initial_pt = initial_pt or {}

    for name_key, values in pairs(initial_pt) do
      local serie_idx = map_rrd_column_to_metrics(schema, name_key)
      local name = schema._metrics[serie_idx]
      local max_val = ts_common.getMaxPointValue(schema, name, tags)
      local ptval = ts_common.normalizeVal(values[1], max_val, options)

      table.insert(series[serie_idx].data, 1, ptval)
    end

    count = count + 1

    if total_serie then
      -- recalculate with additional point
      total_serie = makeTotalSerie(series, count)
    end
  end

  if options.calculate_stats then
    stats = table.merge(stats, ts_common.calculateMinMax(total_serie))
  end

  return {
    start = fstart,
    step = fstep,
    count = count,
    series = series,
    statistics = stats,
    additional_series = {
      total = total_serie,
    },
  }
end

-- ##############################################

-- *Limitation*
-- tags_filter is expected to contain all the tags of the schema except the last
-- one. For such tag, a list of available values will be returned.
local function _listSeries(schema, tags_filter, wildcard_tags, start_time)
  if #wildcard_tags > 1 then
    traceError(TRACE_ERROR, TRACE_CONSOLE, "RRD driver does not support listSeries on multiple tags")
    return nil
  end

  local wildcard_tag = wildcard_tags[1]

  if not wildcard_tag then
    local full_path = driver.schema_get_full_path(schema, tags_filter)
    local last_update = ntop.rrd_lastupdate(full_path)

    if last_update ~= nil and last_update >= start_time then
      return {tags_filter}
    else
      return nil
    end
  end

  if wildcard_tag ~= schema._tags[#schema._tags] then
    traceError(TRACE_ERROR, TRACE_CONSOLE, "RRD driver only support listSeries with wildcard in the last tag, got wildcard on '" .. wildcard_tag .. "'")
    return nil
  end

  local base, rrd = schema_get_path(schema, table.merge(tags_filter, {[wildcard_tag] = ""}))
  local files = ntop.readdir(base)
  local res = {}

  for f in pairs(files or {}) do
    local v = string.split(f, "%.rrd")
    local fpath = base .. "/" .. f

    if((v ~= nil) and (#v == 1)) then
      local last_update = ntop.rrd_lastupdate(fpath)

      if last_update ~= nil and last_update >= start_time then
        -- TODO remove after migration
        local value = v[1]
        local toadd = false

        if wildcard_tag == "l4proto" then
          if L4_PROTO_KEYS[value] ~= nil then
            toadd = true
          end
        elseif ((wildcard_tag ~= "protocol") or ((L4_PROTO_KEYS[value] == nil) and (interface.getnDPIProtoId(value) ~= -1))) and
            ((wildcard_tag ~= "category") or (interface.getnDPICategoryId(value) ~= -1)) then
          toadd = true
        end

        if toadd then
          res[#res + 1] = table.merge(tags_filter, {[wildcard_tag] = value})
        end
      end
    elseif ntop.isdir(fpath) then
      fpath = fpath .. "/" .. rrd .. ".rrd"

      local last_update = ntop.rrd_lastupdate(fpath)

      if last_update ~= nil and last_update >= start_time then
        res[#res + 1] = table.merge(tags_filter, {[wildcard_tag] = f})
      end
    end
  end

  return res
end

-- ##############################################

function driver:listSeries(schema, tags_filter, wildcard_tags, start_time)
  return _listSeries(schema, tags_filter, wildcard_tags, start_time)
end

-- ##############################################

function driver:listSeriesBatched(batch)
  local res = {}

  -- Do not batch, just call listSeries
  for key, item in pairs(batch) do
    res[key] = driver:listSeries(item.schema, item.filter_tags, item.wildcard_tags, item.start_time)
  end

  return res
end

-- ##############################################

function driver:topk(schema, tags, tstart, tend, options, top_tags)
  if #top_tags > 1 then
    traceError(TRACE_ERROR, TRACE_CONSOLE, "RRD driver does not support topk on multiple tags")
    return nil
  end

  local top_tag = top_tags[1]

  if top_tag ~= schema._tags[#schema._tags] then
    traceError(TRACE_ERROR, TRACE_CONSOLE, "RRD driver only support topk with topk tag in the last tag, got topk on '" .. (top_tag or "") .. "'")
    return nil
  end

  local series = _listSeries(schema, tags, top_tags, tstart)
  if not series then
    return nil
  end

  local items = {}
  local tag_2_series = {}
  local total_serie = {}
  local total_valid = true
  local step = 0
  local query_start = tstart

  if options.initial_point then
    query_start =  tstart - schema.options.step
  end

  for _, serie_tags in pairs(series) do
    local rrdfile = driver.schema_get_full_path(schema, serie_tags)

    if isDebugEnabled() then
      traceError(TRACE_NORMAL, TRACE_CONSOLE, string.format("RRD_FETCH[topk] schema=%s %s[%s] -> (%s): last_update=%u",
        schema.name, table.tconcat(tags, "=", ","), table.concat(top_tags, ","), rrdfile, ntop.rrd_lastupdate(rrdfile)))
    end

    touchRRD(rrdfile)

    local fstart, fstep, fdata, fend, fcount = ntop.rrd_fetch_columns(rrdfile, RRD_CONSOLIDATION_FUNCTION, query_start, tend)
    local sum = 0

    if fdata == nil then
      goto continue
    end

    step = fstep

    local partials = {}

    for name_key, serie in pairs(fdata) do
      local serie_idx = map_rrd_column_to_metrics(schema, name_key)
      local name = schema._metrics[serie_idx]
      local max_val = ts_common.getMaxPointValue(schema, name, serie_tags)
      partials[name] = 0

      -- Remove the last value: RRD seems to give an additional point
      serie[#serie] = nil

      if (#total_serie ~= 0) and #total_serie ~= #serie then
        -- NOTE: even if touchRRD is used, series can still have a different number
        -- of points when the tend parameter does not correspond to the current time
        -- e.g. when comparing with the past or manually zooming.
        -- In this case, total serie il discarded as it's incorrect
        total_valid = false
      end

      for i=#total_serie + 1, #serie do
        -- init
        total_serie[i] = 0
      end

      for i, v in pairs(serie) do
        local v = ts_common.normalizeVal(v, max_val, options)

        if type(v) == "number" then
          sum = sum + v
          partials[name] = partials[name] + v * step
          total_serie[i] = total_serie[i] + v
        end
      end
    end

    items[serie_tags[top_tag]] = sum * step
    tag_2_series[serie_tags[top_tag]] = {serie_tags, partials}
    ::continue::
  end

  local topk = {}

  for top_item, value in pairsByValues(items, rev) do
    if value > 0 then
      topk[#topk + 1] = {
        tags = tag_2_series[top_item][1],
        value = value,
        partials = tag_2_series[top_item][2],
      }
    end

    if #topk >= options.top then
      break
    end
  end

  local stats = nil

  local augumented_total = table.clone(total_serie)

  if options.initial_point and total_serie then
    -- remove initial point to avoid stats calculation on it
    table.remove(total_serie, 1)
  end

  local fstep, count = sampleSeries(schema, #augumented_total, step, options.max_num_points, {{data=augumented_total}})

  if options.calculate_stats then
    stats = ts_common.calculateStatistics(total_serie, step, tend - tstart, schema.options.metrics_type)
    stats = table.merge(stats, ts_common.calculateMinMax(augumented_total))
  end

  if not total_valid then
    total_serie = nil
    augumented_total = nil
  end

  return {
    topk = topk,
    additional_series = {
      total = augumented_total,
    },
    statistics = stats,
  }
end

-- ##############################################

function driver:queryTotal(schema, tstart, tend, tags, options)
  local rrdfile = driver.schema_get_full_path(schema, tags)

  if not ntop.exists(rrdfile) then
     return nil
  end

  touchRRD(rrdfile)

  local fstart, fstep, fdata, fend, fcount = ntop.rrd_fetch_columns(rrdfile, RRD_CONSOLIDATION_FUNCTION, tstart, tend)
  local totals = {}

  for name_key, serie in pairs(fdata or {}) do
    local serie_idx = map_rrd_column_to_metrics(schema, name_key)
    local name = schema._metrics[serie_idx]
    local max_val = ts_common.getMaxPointValue(schema, name, tags)
    local sum = 0

    -- Remove the last value: RRD seems to give an additional point
    serie[#serie] = nil

    for i, v in pairs(serie) do
      local v = ts_common.normalizeVal(v, max_val, options)

      if type(v) == "number" then
        sum = sum + v * fstep
      end
    end

    totals[name] = sum
  end

  return totals
end

-- ##############################################

local function deleteAllData(ifid)
  local paths = getRRDPaths()

  for _, path in pairs(paths) do
    local ifpath = os_utils.fixPath(dirs.workingdir .. "/" .. ifid .. "/".. path .."/")
    local path_to_del = os_utils.fixPath(ifpath)

    if ntop.exists(path_to_del) and not ntop.rmdir(path_to_del) then
      return false
    end
  end

  return true
end

function driver:delete(schema_prefix, tags)
  -- NOTE: delete support in this driver is currently limited to a specific set of schemas and tags
  local supported_prefixes = {
    host = {
      tags = {ifid=1, host=1},
      path = function(tags) return getRRDName(tags.ifid, tags.host) end,
    }, mac = {
      tags = {ifid=1, mac=1},
      path = function(tags) return getRRDName(tags.ifid, tags.mac) end,
    }, snmp_if = {
      tags = {ifid=1, device=1},
      path = function(tags) return getRRDName(tags.ifid, "snmp:" .. tags.device) end,
    }, host_pool = {
      tags = {ifid = 1, pool = 1},
      path = function(tags) return getRRDName(tags.ifid, "pool:" .. tags.pool) end,
    }, subnet = {
      tags = {ifid=1, subnet=1},
      path = function(tags) return getRRDName(tags.ifid, "net:" .. tags.subnet) end,
    }
  }

  if schema_prefix == "" then
    -- Delete all data
    return deleteAllData(tags.ifid)
  end

  local delete_info = supported_prefixes[schema_prefix]

  if not delete_info then
    traceError(TRACE_ERROR, TRACE_CONSOLE, "unsupported schema prefix for delete: " .. schema_prefix)
    return false
  end

  for tag in pairs(delete_info.tags) do
    if not tags[tag] then
      traceError(TRACE_ERROR, TRACE_CONSOLE, "missing tag '".. tag .."' for schema prefix " .. schema_prefix)
      return false
    end
  end

  for tag in pairs(tags) do
    if not delete_info.tags[tag] then
      traceError(TRACE_ERROR, TRACE_CONSOLE, "unexpected tag '".. tag .."' for schema prefix " .. schema_prefix)
      return false
    end
  end

  local path_to_del = os_utils.fixPath(delete_info.path(tags))
  if ntop.exists(path_to_del) and not ntop.rmdir(path_to_del) then
     return false
  end

  if isDebugEnabled() then
    traceError(TRACE_NORMAL, TRACE_CONSOLE, string.format("DELETE schema=%s, %s => %s",
      schema_prefix, table.tconcat(tags, "=", ","), path_to_del))
  end

  return true
end

-- ##############################################

function driver:deleteOldData(ifid)
  local paths = getRRDPaths()
  local dirs = ntop.getDirs()
  local ifaces = ntop.listInterfaces()
  local retention_days = tonumber(ntop.getPref("ntopng.prefs.old_rrd_files_retention")) or 365

  for _, path in pairs(paths) do
    local ifpath = os_utils.fixPath(dirs.workingdir .. "/" .. ifid .. "/".. path .."/")
    local deadline = retention_days * 86400

    if isDebugEnabled() then
      traceError(TRACE_NORMAL, TRACE_CONSOLE, string.format("ntop.deleteOldRRDs(%s, %u)", ifpath, deadline))
    end

    ntop.deleteOldRRDs(ifpath, deadline)
  end

  return true
end

-- ##############################################

function driver:setup(ts_utils)
  return true
end

-- ##############################################

return driver
