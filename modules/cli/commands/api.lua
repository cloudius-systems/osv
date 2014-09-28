local OptionParser = require('std.optparse')
local json = require("json")

local function main_usage(schema)
  local tt = {{"API", "DESCRIPTION"}}
  for _, resource in ipairs(schema.apis) do
    table.insert(tt, {resource.cli_alias, resource.description})
  end
  io.write(table_format(tt), "\n")
end

local function resource_usage(resource)
  local tt = {{"COMMAND", "DESCRIPTION"}}
  for _, api in ipairs(resource.definition.apis) do
    local cli_alias = table.concat(api.cli_alias, ' ')

    for i = 1, #api.operations do
      table.insert(tt, {
        (i == 1 and cli_alias or ""),
        (api.operations[i].method == "GET" and ""
          or "[" .. api.operations[i].method .. "] ") ..
        api.operations[i].summary
      })
    end
  end

  io.write(table_format(tt), "\n")
end

local function api_usage(resource, show_api)
  io.write("Resource: ", resource.description, "\n")
  for iapi, api in ipairs(resource.definition.apis) do
    if show_api.path == api.path then
      io.write("API: ", api.path, "\n")
      io.write("Operations:\n")
      for iop, op in ipairs(api.operations) do
        io.write("  Method:  ", op.method, "\n")
        io.write("  Summary: ", op.summary, "\n")
        if op.parameters and #op.parameters > 0 then
          io.write("  Parameters:\n")
          for iparam, param in ipairs(op.parameters) do
            io.write("    Name:         ", param.name, "\n")
            io.write("    Description:  ", param.description, "\n")
            io.write("    Required:     ", tostring(param.required), "\n")
            io.write("    Multiplicity: ", tostring(param.allowMultiple), "\n")
            io.write("    Type:         ", param.type, "\n")
            io.write("    ParamType:    ", param.paramType, "\n")
            if param.enum and #param.enum > 0 then
              io.write("    Values:       ", table.concat(param.enum, ", "), "\n")
            end
            if iparam < #op.parameters then
              io.write("\n")
            end
          end
        end

        if iop < #api.operations then
          io.write("\n")
        end
      end
    end
  end
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
local function args_api_matches(arguments, parameters, api_path)
  local api_args = split(api_path, "[^/]+")
  local move_parameters = {}

  for i = 1, #api_args do
    local name_arg = string.match(api_args[i], "^{.*}$") and
      string.sub(api_args[i], 2, -2)

    -- If this is not a name_arg and the order of the arguments don't match
    if not name_arg and arguments[i] ~= api_args[i] then
      return false
    -- If this is a name_arg but there's no parameter or argument for it
    elseif name_arg and (not parameters[name_arg] and not arguments[i]) then
      return false
    elseif name_arg and parameters[name_arg] then
      move_parameters[i] = name_arg
    end
  end

  for i in pairs(move_parameters) do
    if #parameters[move_parameters[i]] >= 1 then
      arguments[i] = parameters[move_parameters[i]][1]
    end
    parameters[move_parameters[i]] = nil
  end

  if #api_args ~= #arguments then
    return false
  end

  return true
end

local function find_api(resource, arguments, parameters)
  for _, api in ipairs(resource.definition.apis) do
    if args_api_matches(arguments, parameters, api.path) then
      return api
    end
  end

  return nil
end

local cmd = {}

cmd.desc = [[execute arbitrary OSv API operations as defined by the schema]]

cmd.parser = OptionParser [[
api - execute arbitrary OSv API operations as defined by the schema

Usage: api [OPTION]... API ACTION-WORD [ACTION-WORD]... [PARAMETER=value]...

Execute arbitrary OSv API operations according to the defined schema.

Options:

  -m, --method=[METHOD]  The method to use (Default: GET)
  -r, --raw              Do not process the response
  -h                     Show help of command or sub-command
      --help             Show this help
]]

-- Remove file method from parser or it won't accept "file" as an argument
local tmp = getmetatable(cmd.parser)
tmp.__index.file = nil
setmetatable(cmd.parser, tmp)
tmp = nil

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  -- Local variables
  local do_raw = opts.raw
  local do_help = opts.h
  local selected_method = opts.method and opts.method or "GET"

  -- Load the schema
  local schema = osv_schema()

  -- Separate arguments and parameters
  local arguments = {} -- Real arguments
  local parameters = {} -- Parameters for the request, can be multiple
  for i = 1, #args do
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

  -- Find selected resource and API
  local selected_resource, selected_api
  if #arguments >= 1 then
    selected_resource = find_resource(schema, arguments[1])
    if not selected_resource then
      io.stderr:write("Unknown API: ", arguments[1], "\n")
      main_usage(schema)
      return
    end

    selected_api = find_api(selected_resource, arguments, parameters)
    if not selected_api then
      resource_usage(selected_resource)
      return
    end
  end

  -- Find selected operation
  local selected_operation
  for _, op in ipairs(selected_api.operations) do
    if op.method == selected_method then
      selected_operation = op
    end
  end

  -- Verify that this action is supported (or display help)
  if not selected_operation or do_help then
    if not selected_operation then
      io.stderr:write("Unsupported method: ", selected_method, "\n")
    end

    api_usage(selected_resource, selected_api)
    return
  end

  -- Collect parameters
  local op_params = {}

  -- Validate parameters in operation
  if selected_operation.parameters then
    for _, param in ipairs(selected_operation.parameters) do
      if param.paramType and param.paramType == "path" then
        -- Skip; taken care of in args_api_matches()
      else
        if param.required and not parameters[param.name] then
          error("Required parameter not supplied: " .. param.name)
        end

        if not param.allowMultiple and parameters[param.name] and
           #parameters[param.name] > 1 then
          error("Parameter multiplicity not supported for: " .. param.name)
        end

        op_params[param.name] = true
      end
    end
  end

  -- Find wrong parameters
  for param, values in pairs(parameters) do
    assert(op_params[param], "Unknown parameter supplied: " .. param)
  end

  -- Now that we know everything is ok, perform the requested operation
  -- and print the result
  local response, status = osv_request(arguments, selected_method,
    parameters, do_raw)

  if status == 404 then
    if response.message then
      error(response.message)
    elseif selected_operation.errorResponses then
      for _, err in selected_operation.errorResponses do
        if err.code == status then
          error(err.reason)
        end
      end
    end

    -- Fallback
    error("HTTP 404: Not found")
  end

  if do_raw then
    io.write(response, "\n")
  else
    io.write(render_response(response, selected_resource, selected_api,
      selected_operation), "\n")
  end
end

return cmd
