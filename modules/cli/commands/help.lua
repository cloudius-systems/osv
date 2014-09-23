local context = require('context')
local lfs = require('lfs')
local OptionParser = require('std.optparse')

local function sort_commands(a, b)
  return a[1] < b[1]
end

local cmd = {}

cmd.desc = "list console available commands and show help"

cmd.parser = OptionParser[[
help - list console available commands and show help

Usage: help [COMMAND]

Lists the available commands and show a command help if supplied.
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  if #args == 0 then
    local tt = {}
    for file in lfs.dir(context.commands_path) do
      if file ~= "." and file ~= ".." then
        local command
        local status, err = pcall(function() command = dofile(context.commands_path .. "/" .. file) end)
        if not status then
          io.stderr:write("Failed to load command (", file, "): ", err, "\n")
        else
          table.insert(tt, {string.sub(file, 1, -5), command.desc and command.desc or ""})
        end
      end
    end

    table.sort(tt, sort_commands)
    table.insert(tt, 1, {"COMMAND", "DESCRIPTION"})
    io.write(table_format(tt), "\n")
  else
    local command
    local cmdname = args[1]
    local cmdfile = command_filename(cmdname)

    assert(file_exists(cmdfile), "Command (" .. cmdname .. ") not found")

    local status, err = pcall(function() command = dofile(cmdfile) end)
    if not status then
      error("Failed to load command (" .. cmdname .. "): " .. err)
    end

    if command.parser then
      io.write(command.parser.helptext, "\n")
    elseif command.help then
      io.write(command.help, "\n")
    else
      io.stderr:write("No help found for command: ", cmdname, "\n")
    end
  end
end

return cmd
