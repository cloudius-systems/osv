local t = {}

local etc_response = '{"group": "osv", "permission": "755", "blockSize": 512, "accessTime": 1410341362, "pathSuffix": "etc", "modificationTime": 1410341369, "replication": 4, "length": 10, "owner": "osv", "type": "DIRECTORY"}'

t.run = function()
  osv_request_mock(etc_response, {"file", "/etc"}, "GET", {op = "GETFILESTATUS"})
  osv_request_mock()

  -- Relative path
  t_main({"etc"})
  assert(cwd.get() == "/etc")

  -- Absolue path
  t_main({"/etc"})
  assert(cwd.get() == "/etc")
end

return t
