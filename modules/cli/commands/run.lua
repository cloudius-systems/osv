local OptionParser = require('std.optparse')

local cmd = {}

cmd.desc = [[run an executable in the background]]

cmd.parser = OptionParser [[
run - run an executable in the background

Usage: run "command.so args..." ...
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  for i = 1, #args do
    osv_request({"app"}, "PUT", {
      command = args[i]
    })
  end
end

return cmd
