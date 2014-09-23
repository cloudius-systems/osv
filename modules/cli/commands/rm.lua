local OptionParser = require('std.optparse')

local cmd = {}

cmd.desc = [[remove files or directories]]

cmd.parser = OptionParser [[
rm - remove files or directories

Usage: rm FILE...
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  for i = 1, #args do
    osv_request({"file", args[i]}, "DELETE", {op = "DELETE"})
  end
end

return cmd
