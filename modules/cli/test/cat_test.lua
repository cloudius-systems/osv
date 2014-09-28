local t = {}

t.run = function()
  osv_request_mock("foohost", {"file", "/etc/hosts"}, "GET", {op = "GET"})
  assert(t_main({"/etc/hosts"}) == "foohost\n")

  osv_request_mock({response="none", status=404}, {"file", "/no/file"}, "GET", {op = "GET"})
  local out, err = t_main({"/no/file"})
  assert(err == "/no/file: File not found\n")
end

return t
