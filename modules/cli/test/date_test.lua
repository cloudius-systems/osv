local t = {}

t.run = function()
  osv_request_mock('"Thu Sep 11 10:24:47 2014"', {"os", "date"}, "GET")
  assert(t_main() == "Thu Sep 11 10:24:47 2014\n")
end

return t
