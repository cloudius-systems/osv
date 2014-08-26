local http = require("socket.http")
local url = require("socket.url")
local json = require("json")
local ltn12 = require("ltn12")

local server = "127.0.0.1:8000"
local schema = {}

function osv_schema()
  if next(schema) == nil then
    io.stderr:write("Loading OSv schema from: " .. server .. "\n")
    schema, err = osv_request("/api/api-docs.json", "GET")
    assert(schema, "Failed to load schema from: " .. server)
    for _, resource in ipairs(schema.apis) do
      api_def_url = "/api/" .. string.sub(resource.path, 5)
      resource.definition = osv_request(api_def_url, "GET")
      resource.cli_alias = string.sub(resource.definition.resourcePath, 2)

      for _, api in ipairs(resource.definition.apis) do
        api.cli_alias = split(string.sub(api.path, 2), "[^/]+")
      end
    end
  end

  return schema
end

--- Perform an OSv API request
-- Sends a request to the OSv API and returns the body of the response. By
-- default, try to decode the response as JSON unless `do_raw` is set to true.
--
-- @param arguments  Table of arguments or a string to construct the path. If a
--                   table is provided, join its fields with '/' after escaping
--                   them. Otherwise, cast to string and don't escape.
--                   Example: {"os", "version"} -> /os/version
-- @param method     The method for the request (e.g. GET, POST)
-- @param parameters The request parameters. A table of key/value where key is
--                   param name, and value can be either array or string. If
--                   value is a table, specify this parameter multiple times,
--                   otherwise use as is once. Values will be escaped.
--                   Example: {path = "/etc/hosts"} -> "?path=%2fetc%2fhosts"
--                   Example: {foo = {"bar1", "bar2"}} -> "?foo=bar1&foo=bar2"
-- @param do_raw     Return the raw response, without decoding (default: false)
--
-- @return object|string Body of the response, type depends on do_raw and
--                       response itself if processed.
function osv_request(arguments, method, parameters, do_raw)
  local raw = do_raw or false

  -- Construct path, join /arg/ume/nts ;)
  local path = type(arguments) == 'table'
    and ("/" .. table.concat(map(url.escape, arguments), "/"))
    or tostring(arguments)

  --[[ Construct query parameters:
    `parameters` is key/value where value is table, to enable multiplicity,
    or a simple string ]]--
  local reqquery = ""
  if type(parameters) == "table" then
    local tbl_reqquery = {}
    for k, a in pairs(parameters) do
      if type(a) == "table" then
        for _, v in ipairs(a) do
          table.insert(tbl_reqquery, k .. "=" .. url.escape(v))
        end
      else
        table.insert(tbl_reqquery, k .. "=" .. url.escape(a))
      end
    end
    reqquery = table.concat(tbl_reqquery, "&")
  end

  -- TODO: Construct body (POST) parameters, not usable with current API anyway
  local reqbody = ""

  -- Construct headers
  local reqheaders = {}
  if method == 'POST' then
    reqheaders['content-type'] = 'application/x-www-form-urlencoded'
  end

  -- Construct full URL, with query parameters if exists
  local full_url = "http://" .. server .. path
  if string.len(reqquery) > 0 then
    full_url = full_url .. "?" .. reqquery
  end

  -- Response body sink
  local respbody = {}

  -- Perform the request
  local respcode, status, headers = http.request {
    method = method,
    url = full_url,
    source = ltn12.source.string(reqbody),
    sink = ltn12.sink.table(respbody)
  }

  if raw then
    return table.concat(respbody), status, headers
  else
    -- Try decoding, if it fails return string as is
    local bodystr, decoded = table.concat(respbody), nil
    local s, e = pcall(function() decoded = json.decode(bodystr) end)
    if s then
      return decoded, status, headers
    else
      return bodystr, status, headers
    end
  end
end

--- Shorthand assert for response codes
function osv_resp_assert(status, ...)
  for _, c in ipairs({...}) do
    if status == c then return end
  end
  error("Unknown response code: " .. status)
end

-- Console renderers for response classes
local renderer = {
  string = function(response)
    return response
  end,
  long = function(response)
    return tostring(response)
  end,
  dateTime = function(response)
    return tostring(response)
  end,
  [""] = function(response)
    return response
  end
}

-- Translates a response to console representation
function render_response(response, response_class)
  if not renderer[response_class] then
    io.stderr:write("Missing renderer for response class: "
      .. response_class .. "\n")
    return DataDumper(response)
  end

  return renderer[response_class](response)
end

function osv_setserver(s)
  server = s
end

function osv_getserver()
  print(server)
end
