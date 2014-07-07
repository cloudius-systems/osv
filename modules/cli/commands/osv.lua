local json = require("json")

local sh_opts = "hrm:"
local long_opts = {
  method = "m",
  help = "h",
  raw = "r"
}

local function main_usage(schema)
  print("Available commands:")
  for _, resource in ipairs(schema.apis) do
    print(resource.cli_alias, resource.description)
  end
end

local function resource_usage(resource)
  print("Available commands:")

  local tt = {}
  for _, api in ipairs(resource.definition.apis) do
    local cli_alias = table.concat(api.cli_alias, ' ')
    if #api.operations == 1 then
      table.insert(tt, {
        cli_alias,
        (api.operations[1].method == "GET" and ""
          or "(" .. api.operations[1].method .. ") ") ..
        api.operations[1].summary
      })
    else
      table.insert(tt, {cli_alias, "", ""})
      for i = 1, #api.operations do
        table.insert(tt,
          {"", api.operations[i].method, api.operations[i].summary})
      end
    end
  end

  table_print(tt)
end

local function api_usage(api)
  -- TODO
  print(api.path)
end

-- Finds resource by arguments. Assumes 1-word resource path (/os, /jvm, etc.)
local function find_resource(schema, str)
  for _, resource in ipairs(schema.apis) do
    if resource.cli_alias == str then
      return resource
    end
  end
  return nil
end

-- Determines if arguments match an api path
local function args_api_matches(arguments, api_path)
  local api_args = split(api_path, "[^/]+")
  if #arguments ~= #api_args then
    return false
  else
    for i = 1, #arguments do
      if not string.match(api_args[i], "^{.*}$") and
        arguments[i] ~= api_args[i] then
        return false
      end
    end
  end

  return true
end

local function find_api(resource, arguments)
  for _, api in ipairs(resource.definition.apis) do
    if args_api_matches(arguments, api.path) then
      return api
    end
  end

  return nil
end

--[[
Synopsis:

osv [OPTIONS] <api> <action-word [action-word, ...]> [parameter=value, ...]
]]--

return {
  run = function(args)
    -- Local variables
    local do_help = false
    local do_raw = false
    local selected_method = "GET"

    -- Load the schema
    local schema = osv_schema()

    -- Scan the args
    local optarg, optind = alt_getopt.get_opts(args, sh_opts, long_opts)
    assert(optarg, optind)

    -- Register flags
    for k, v in pairs(optarg) do
      if k == "h" then do_help = true end
      if k == "m" then selected_method = v end
      if k == "r" then do_raw = true end
    end

    -- Separate arguments and parameters
    local arguments = {} -- Real arguments
    local parameters = {} -- Parameters for the request, can be multiple
    for i = optind, #args do
      local s, e = string.find(args[i], '=')
      if s then
        local param = string.sub(args[i], 1, s-1)
        local value = string.sub(args[i], s+1)

        if parameters[param] then
          table.insert(parameters[param], value)
        else
          parameters[param] = {value}
        end
      else
        table.insert(arguments, args[i])
      end
    end

    if #arguments == 0 then
      main_usage(schema)
      return
    end

    -- Find selected resource
    local selected_resource
    if #arguments >= 1 then
      selected_resource = find_resource(schema, arguments[1])
      if not selected_resource then
        print("Unknown command: " .. arguments[1])
        main_usage(schema)
        return
      end

      if #arguments == 1 then
        resource_usage(selected_resource)
      end
    end

    -- Find selected API
    local selected_api
    if #arguments >= 2 then
      selected_api = find_api(selected_resource, arguments)
      if not selected_api then
        print("Unknown command: " .. table.concat(arguments))
        resource_usage(selected_resource)
        return
      end

      if do_help then
        api_usage(selected_api)
      end

      -- Find selected operation
      local selected_operation
      for _, op in ipairs(selected_api.operations) do
        if op.method == selected_method then
          selected_operation = op
        end
      end

      -- Verify that this action is supported
      assert(selected_operation,
        "Unsupported action method: " .. selected_method)

      -- Collect parameters
      local op_params = {}

      -- Validate parameters in operation
      for _, param in ipairs(selected_operation.parameters) do
        if param.required and not parameters[param.name] then
          error("Required parameter not supplied: " .. param.name)
        end

        if not param.allowMultiple and #parameters[param.name] > 1 then
          error("Parameter multiplicity not supported for: " .. param.name)
        end

        -- TODO: Validate parameter types

        op_params[param.name] = true
      end

      -- Find wrong parameters
      for param, values in pairs(parameters) do
        assert(op_params[param], "Unknown parameter supplied: " .. param)
      end

      -- Now that we know everything is ok, perform the requested operation
      -- and print the result
      local response = osv_request(arguments, selected_method, parameters, do_raw)
      if do_raw then
        print(response)
      else
        print(render_response(response, selected_operation.responseClass))
      end
    end
  end
}