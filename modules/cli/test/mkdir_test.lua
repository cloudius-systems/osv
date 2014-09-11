local t = {}

t.run = function()
  osv_request_mock("mock", {"file", "/foo"}, "PUT", {
      op = "MKDIRS", permission = "0755", createParent = "false"})
  osv_request_mock("mock", {"file", "/foo/parent"}, "PUT", {
      op = "MKDIRS", permission = "0755", createParent = "true"})

  -- Failing to find a mocked response will raise and error
  t_main({"/foo"})
  t_main({"-p", "/foo/parent"})
end

return t
