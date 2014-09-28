local OptionParser = require('std.optparse')

local cmd = {}

cmd.desc = [[change the shell working directory]]

cmd.parser = OptionParser [[
cd - change the shell working directory

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
      io.stderr:write(path, ": no such file or directory\n")
    elseif content.type ~= "DIRECTORY" then
      io.stderr:write(path, ": Not a directory\n")
    else
      cwd.set(rpath)
    end
  end
end

return cmd
