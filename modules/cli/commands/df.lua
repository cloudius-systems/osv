-- TODO: Print sizes in different format
local OptionParser = require('std.optparse')

local cmd = {}

cmd.parser = OptionParser [[
df - report file system disk space usage

Usage: df [FILE]...

Show information about the file system mount, or for all mounts by default.
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  if #args == 0 then
    table.insert(args, "")
  end

  local df = {}
  for _, arg in ipairs(args) do
    local res, status = osv_request({"fs", "df", arg}, "GET")
    osv_resp_assert(status, 200, 404)

    if status == 404 then
      io.stderr:write(path .. ": Mount point not found\n")
    else
      for _, fs in ipairs(res) do
        table.insert(df, fs)
      end
    end
  end

  local tt = {{"Filesystem", "Total", "Used", "Use%", "Mounted on"}}
  for _, fs in ipairs(df) do
    table.insert(tt, {
      fs.filesystem,
      tostring(fs.btotal),
      tostring(fs.btotal - fs.bfree),
      string.format("%.2f%%", 100 - ((fs.bfree / fs.btotal) * 100)),
      fs.mount
    })
  end

  print(table_format(tt))
end

return cmd
