local t = {}

t.run = function()
  osv_request_mock("foohost", {"file", "/etc/hosts"}, "GET", {op = "GET"})
  assert(t_main({"/etc/hosts"}) == "foohost\n")
end

return t
