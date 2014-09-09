require('util')
require('osv_api')
require('data_dumper')

-- Global modules
cwd = require('cwd')
context = require('context')

-- Local modules
local lpeg = require('lpeg')

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

--- Splits a string command line to arguments
--
-- @param line String to be splitted
-- @return List of arguments as a table
local cla_singlequoted_arg = lpeg.P("'") * lpeg.Cs(((1 - lpeg.S("'\\")) + (lpeg.P("\\'") / "'")) ^ 0) * "'"
local cla_doublequoted_arg = lpeg.P('"') * lpeg.Cs(((1 - lpeg.S('"\\')) + (lpeg.P('\\"') / '"')) ^ 0) * '"'
local cla_free_arg         = lpeg.C((1 - lpeg.S(' \t'))^0)
local cla_arg              = cla_singlequoted_arg + cla_doublequoted_arg + cla_free_arg
local cla_complete         = lpeg.Ct(cla_arg * (lpeg.S(' \t')^1 * cla_arg)^0)
local function command_line_argv(line)
  local s = string.gsub(line, '\\\\', '\\')
  return lpeg.match(cla_complete, s)
end

--- Overrides OptParse:parse()
-- Used when parsing will be done in cli_command()
-- to avoid it being parsed twice
local function optparse_parse(self)
  return self.unrecognised, self.opts
end

--- Returns the prompt
function prompt()
  return (cwd.get() .. '# ')
end

--- Processes a command
-- Executes a command in a protected call (pcall).
--
-- @param args String or table of arguments. If string, processed and splitted
--             to arguments.
function cli_command(args)
  local command = ""
  local arguments = args

  if type(args) == "string" then
    arguments = command_line_argv(args)
  end

  if #arguments > 0 then
    command = arguments[1]
    table.remove(arguments, 1)

    filename = command_filename(command)
    if file_exists(filename) then
      local cmd = dofile(filename)
      local cmd_run = true

      -- Override --help implementation (by default it uses os.exit())
      if cmd.parser then
        cmd.parser:on({"--help"}, function(self, arglist, i, value)
          cmd_run = false
          io.stderr:write(self.helptext .. '\n')
          return i + 1
        end)

        local args, opts = cmd.parser:parse(arguments)
        cmd.parser.parse = optparse_parse
      end

      if cmd_run then
        local status, err = pcall(function() cmd.main(arguments) end)
        if not status then
          print_lua_error(command, err)
        end
      end
    else
      print_cmd_err(command, "command not found")
    end
  end
end

function cli_command_single(args, optind)
  local t = {}
  for i = optind, #args do
    if args[i] ~= '--' then
      table.insert(t, args[i])
    end
  end
  cli_command(t)
end
