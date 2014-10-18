local cmd = {}

cmd.desc = [[reboot an OSv instance]]
cmd.help = [[Usage: reboot

Reboot the instance.]]

cmd.main = function()
  local total, status = osv_request({"os", "reboot"}, "POST")
  osv_resp_assert(status, 200)
end

return cmd
