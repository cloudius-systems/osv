require('util')
require('osv_api')
require('alt_getopt')
require('data_dumper')

-- Global modules
cwd = require('cwd')
context = require('context')

local function command_filename(name)
  return string.format('%s/%s.lua', context.commands_path, name)
end

--- Prints message to stderr
local function print_cmd_err(cmd, msg)
  io.stderr:write(cmd .. ": " .. msg .. "\n")
end

--- Removes Lua code reference in the error message and prints it
local function print_lua_error(cmd, msg)
  local pos, last = string.find(msg, ":[0-9]+: ")
  if last then
    print_cmd_err(cmd, string.sub(msg, last+1))
  else
    print_cmd_err(cmd, msg)
  end
end

--- Returns the prompt
function prompt()
  return (cwd.get() .. '# ')
end

--- Processes a line as a command
function cli(line)
  local command = trim(line)
  local arguments = ""

  if command:len() > 0 then
    local s, e = command:find(" ")

    if s then
      arguments = trim(command:sub(s))
      command = trim(command:sub(0, s))
    end

    filename = command_filename(command)
    if file_exists(filename) then
      local cmd = dofile(filename)
      local status, err = pcall(function() cmd.main(split(arguments)) end)
      if not status then
        print_lua_error(command, err)
      end
    else
      print_cmd_err(command, "command not found")
    end
  end
end
