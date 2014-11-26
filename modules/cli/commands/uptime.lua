local cmd = {}

cmd.desc = [[tell how long the system has been running]]
cmd.help = [[Usage: uptime

Print how long the system has been running.]]

cmd.main = function()

  local uptime_secs, status = osv_request({"os", "uptime"}, "GET")
  osv_resp_assert(status, 200)

  local date, status = osv_request({"os", "date"}, "GET")
  osv_resp_assert(status, 200)

  local updays = math.floor(uptime_secs / (60 * 60 * 24))

  local result = " up"

  if updays ~= 0 then
      result = result .. string.format(" %d day%s,", updays, (updays ~= 1) and "s" or "")
  end

  local upminutes = math.floor(uptime_secs / 60);
  local uphours = math.floor((upminutes / 60)) % 24;
  local upminutes = upminutes % 60;

  if uphours ~= 0 then
      result = result .. string.format(" %2d:%02d ", uphours, upminutes)
  else
      result = result .. string.format(" %d min ", upminutes)
  end

  io.write(string.match(date, "(%d%d:%d%d:%d%d)") .. result .. "\n")

end

return cmd
