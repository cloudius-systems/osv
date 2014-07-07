require('util')
require('osv_api')
require('alt_getopt')
require('data_dumper')
commands_path = "commands"

local function command_filename(name)
  return string.format('%s/%s.lua', commands_path, name)
end

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
      cmd.run(split(arguments))
    else
      error(command .. ": command not found")
    end
  end
end
