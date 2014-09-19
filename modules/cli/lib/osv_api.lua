local context = require("context")

local http = require("socket.http")
local url = require("socket.url")
local json = require("json")
local ltn12 = require("ltn12")
local socket = require("socket")

local schema = {}

--- Constructs the path, join /arg/ume/nts ;)
local function construct_path(arguments)
  return type(arguments) == 'table'
    and ("/" .. table.concat(map(url.escape, arguments), "/"))
    or tostring(arguments)
end

--- Construct query parameters
-- `parameters` is key/value where value is table, to enable multiplicity,
-- or a simple string
local function construct_query_params(parameters)
  local ret = ""
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
    ret = table.concat(tbl_reqquery, "&")
  end
  return ret
end

--- Construct full URL, with query parameters if exists
local function construct_full_url(path, reqquery)
  local ret = context.api .. path
  if reqquery and string.len(reqquery) > 0 then
    ret = ret .. "?" .. reqquery
  end
  return ret
end

function osv_schema()
  if next(schema) == nil then
    io.stderr:write("Loading OSv schema from: " .. context.api .. "\n")
    schema, err = osv_request("/api/api-docs.json", "GET")
    assert(schema, "Failed to load schema from: " .. context.api)
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

function create_socket()
  s = socket.tcp()
  s:setoption('tcp-nodelay', true)
  return s
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

  local path = construct_path(arguments)
  local reqquery = construct_query_params(parameters)
  local full_url = construct_full_url(path, reqquery)

  -- TODO: Construct body (POST) parameters, not usable with current API anyway
  local reqbody = ""

  -- Construct headers
  local reqheaders = {}
  if method == 'POST' then
    reqheaders['content-type'] = 'application/x-www-form-urlencoded'
  end

  -- Response body sink
  local respbody = {}

  -- Perform the request
  local respcode, status, headers = http.request {
    method = method,
    url = full_url,
    source = ltn12.source.string(reqbody),
    sink = ltn12.sink.table(respbody),
    create = create_socket
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
  Threads = function(response)
    local tt = {{"id", "name", "cpu", "cpu_ms", "status"}}
    for _, thread in ipairs(response["list"]) do
      table.insert(tt, {
        tostring(thread["id"]),
        thread["name"],
        tostring(thread["cpu"]),
        tostring(thread["cpu_ms"]),
        thread["status"]
      })
    end
    return table_format(tt)
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

-- Testing

--- Overrides http.request with mock responses
local osv_request_mocked = false
local osv_request_mocks = {}
function osv_request_mock(response, arguments, method, parameters)
  if not method then method = "GET" end
  if not parameters then parameters = {} end

  osv_request_mocks[method .. ' '
    .. construct_full_url(construct_path(arguments),
        construct_query_params(parameters))] = response

  if not osv_request_mocked then
    http.request = function(opts)
      local res = osv_request_mocks[opts.method .. ' ' .. opts.url]
      if res then
        opts.sink(res)
        return 1, 200, {}
      else
        error("Mock for '" .. opts.url .. "' not found")
      end
    end
    osv_request_mocked = true
  end
end
