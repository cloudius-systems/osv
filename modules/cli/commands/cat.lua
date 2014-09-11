local OptionParser = require('std.optparse')

local cmd = {}

cmd.parser = OptionParser [[
cat - concatenate files and print on the standard output

Usage: cat [FILE]...

Concatenate FILE(s), or standard input, to standard output.
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  for _, arg in ipairs(args) do
    local path, rpath = arg, cwd.resolve(arg)
    local content, status = osv_request({"file", rpath}, "GET", {op = "GET"}, true)
    osv_resp_assert(status, 200, 404)

    if status == 404 then
      io.stderr:write(path .. ": File not found" .. "\n")
    else
      io.write(content, '\n')
    end
  end
end

return cmd
