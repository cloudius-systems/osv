local http = require("socket.http")
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

function osv_request(arguments, method, parameters, do_raw)
  local raw = do_raw or false

  -- Construct path, join /arg/ume/nts ;)
  local path = type(arguments) == 'table'
    and ("/" .. table.concat(arguments, "/")) or tostring(arguments)

  --[[ Construct query parameters:
    `parameters` is key/value where value is table, to enable multiplicity ]]--
  local reqquery = ""
  if type(parameters) == 'table' then
    local tbl_reqquery = {}
    for k, a in pairs(parameters) do
      for _, v in ipairs(a) do
        table.insert(tbl_reqquery, k .. "=" .. v)
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
  local respcode, headers, status = http.request {
    method = method,
    url = full_url,
    source = ltn12.source.string(reqbody),
    sink = ltn12.sink.table(respbody)
  }

  if raw then
    return table.concat(respbody)
  else
    return json.decode(table.concat(respbody))
  end
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
