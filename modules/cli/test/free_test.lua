local t = {}

t.run = function()
  osv_request_mock(2132283392, {"os", "memory", "total"}, "GET")
  osv_request_mock(1726300160, {"os", "memory", "free"}, "GET")

  assert(t_main() == ('     total      used      free       \n' ..
                      'Mem: 2132283392 405983232 1726300160 \n'))

end

return t
