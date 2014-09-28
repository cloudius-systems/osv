local t = {}

local etc_response = [[{"group": "osv", "permission": "755", "blockSize": 512,
                       "accessTime": 1410341362, "pathSuffix": "etc",
                       "modificationTime": 1410341369, "replication": 4,
                       "length": 10, "owner": "osv", "type": "DIRECTORY"}]]

local fil_response = [[{"group": "osv", "permission": "664", "blockSize": 512,
                       "accessTime": 1411904666, "pathSuffix": "hosts",
                       "modificationTime": 1411904666, "replication": 1,
                       "length": 30, "owner": "osv", "type": "FILE"}]]

t.run = function()
  osv_request_mock(etc_response,
    {"file", "/etc"}, "GET", {op = "GETFILESTATUS"})

  osv_request_mock(fil_response,
    {"file", "/etc/hosts"}, "GET", {op = "GETFILESTATUS"})

  osv_request_mock({response="none", status=404},
    {"file", "/no/file"}, "GET", {op = "GETFILESTATUS"})

  -- Relative path
  t_main({"etc"})
  assert(cwd.get() == "/etc")

  -- Absolue path
  t_main({"/etc"})
  assert(cwd.get() == "/etc")

  -- cd <file>
  local out, err = t_main({"/etc/hosts"})
  assert(err == "/etc/hosts: Not a directory\n")

  -- cd <non-existent>
  local out, err = t_main({"/no/file"})
  assert(err == "/no/file: no such file or directory\n")
end

return t
