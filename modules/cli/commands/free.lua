local cmd = {}

cmd.desc = [[display amount of free and used memory in system]]

cmd.main = function()
  local total, status = osv_request({"os", "memory", "total"}, "GET")
  osv_resp_assert(status, 200)

  local free, status = osv_request({"os", "memory", "free"}, "GET")
  osv_resp_assert(status, 200)

  io.write(table_format({
    {"", "total", "used", "free"},
    {"Mem:", tostring(total), tostring(total - free), tostring(free)}
  }), '\n')
end

return cmd
