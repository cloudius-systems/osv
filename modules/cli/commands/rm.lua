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
    local content, status =
      osv_request({"file", cwd.resolve(args[i])}, "DELETE", {op = "DELETE"})

    if status == 404 then
      io.stderr:write("rm: cannot remove '", args[i],
        "': No such file or directory\n")
    end
  end
end

return cmd
