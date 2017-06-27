local OptionParser = require('std.optparse')

local cmd = {}

cmd.desc = [[run an executable in the background]]

cmd.parser = OptionParser [[
run - run an executable in the background

Usage: run [OPTION]... "command.so args..." ...

Options:

      --newprogram start program in new ELF namespace
      --help       display this help and exit
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)
  flag_newprogram = opts.newprogram

  for i = 1, #args do
    osv_request({"app"}, "PUT", {
      command = args[i],
      new_program = tostring(flag_newprogram)
    })
  end
end

return cmd
