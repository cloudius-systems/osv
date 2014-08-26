local OptionParser = require("std.optparse")

local cmd = {}

cmd.parser = OptionParser [[
ls

Usage: ls
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)
  local path = cwd.get()

  local content, status = osv_request({"file", path}, "GET", {op = "GETFILESTATUS"})
  osv_resp_assert(status, 200, 404)

  if status == 404 then
    error(path .. ": no such file or directory")
  elseif content.type == "DIRECTORY" then
    local content, status = osv_request({"file", path}, "GET", {op = "LISTSTATUS"})
    osv_resp_assert(status, 200)

    local file_list = {}
    for i = 1, #content do
      table.insert(file_list, content[i].pathSuffix .. (content[i].type == "DIRECTORY" and "/" or ""))
    end

    table.sort(file_list)
    list_print(file_list)
  end
end

return cmd
