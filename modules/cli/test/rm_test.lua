local t = {}

t.run = function()
  osv_request_mock("mock", {"file", "/foo/bar"}, "DELETE", {op = "DELETE"})

  -- Failing to find the mocked response will raise and error
  t_main({"/foo/bar"})
end

return t
