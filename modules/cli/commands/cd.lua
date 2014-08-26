local OptionParser = require('std.optparse')

local cmd = {}

cmd.parser = OptionParser [[
cd

Usage: cd <path>
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  if #args == 0 then
    io.stderr:write(cmd.parser.helptext .. '\n')
  else
    local path, rpath = args[1], cwd.resolve(args[1])
    local content, status = osv_request({"file", rpath}, "GET", {op = "GETFILESTATUS"})
    osv_resp_assert(status, 200, 404)

    if status == 404 then
      error(path .. ": no such file or directory")
    elseif content.type ~= "DIRECTORY" then
      error(path .. ": Not a directory")
    else
      cwd.set(rpath)
    end
  end
end

return cmd
