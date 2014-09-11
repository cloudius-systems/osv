local t = {}

t.run = function()
  osv_request_mock("foobar", "/os/dmesg")
  assert(t_main() == "foobar\n")
end

return t
