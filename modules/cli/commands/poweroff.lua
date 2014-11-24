local cmd = {}

cmd.desc = [[poweroff an OSv instance]]
cmd.help = [[Usage: poweroff

Poweroff the instance.]]

cmd.main = function()
  local total, status = osv_request({"os", "poweroff"}, "POST")
  osv_resp_assert(status, 200)
end

return cmd
