local cmd = {}

cmd.desc = [[print operating system boot log]]

cmd.main = function()
    local content, status = osv_request({"os", "dmesg"}, "GET")
    osv_resp_assert(status, 200)
    io.write(content, "\n")
end

return cmd
