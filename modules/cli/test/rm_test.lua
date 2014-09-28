local t = {}

t.run = function()
  osv_request_mock("mock", {"file", "/foo/bar"}, "DELETE", {op = "DELETE"})
  osv_request_mock({response="mock", status=404}, {"file", "/no/foobar"}, "DELETE", {op = "DELETE"})

  -- Failing to find the mocked response will raise and error
  t_main({"/foo/bar"})

  cwd.set("/foo")
  t_main({"bar"})

  local out, err = t_main({"/no/foobar"})
  assert(err == "rm: cannot remove '/no/foobar': No such file or directory\n")
end

return t
