local t = {}

local file_stat_response = [[
  {
    "group": "osv", "permission": "664", "blockSize": 512,
    "accessTime": 1410341732, "pathSuffix": "hosts",
    "modificationTime": 1410341369, "replication": 1,
    "length": 30, "owner": "osv", "type": "FILE"
  }
]]

local dir_stat_response = [[
  {
    "group": "osv", "permission": "755", "blockSize": 512,
    "accessTime": 1410439620, "pathSuffix": "etc",
    "modificationTime": 1410341369, "replication": 4,
    "length": 10, "owner": "osv", "type": "DIRECTORY"
  }
]]

local dir_list_response = [[
  [
    {
      "group": "osv", "permission": "755", "blockSize": 512,
      "accessTime": 1410439620, "pathSuffix": ".",
      "modificationTime": 1410341369, "replication": 4,
      "length": 10, "owner": "osv", "type": "DIRECTORY"
    },
    {
      "group": "osv", "permission": "700", "blockSize": 512,
      "accessTime": 1410341361, "pathSuffix": "..",
      "modificationTime": 1410341733, "replication": 11,
      "length": 18, "owner": "osv", "type": "DIRECTORY"
    },
    {
      "group": "osv", "permission": "444", "blockSize": 512,
      "accessTime": 0, "pathSuffix": "mnttab",
      "modificationTime": 0, "replication": 0,
      "length": 0, "owner": "osv", "type": "FILE"
    },
    {
      "group": "osv", "permission": "664", "blockSize": 512,
      "accessTime": 1410341371, "pathSuffix": "fstab",
      "modificationTime": 1410341369, "replication": 1,
      "length": 141, "owner": "osv", "type": "FILE"
    },
    {
      "group": "osv", "permission": "664", "blockSize": 512,
      "accessTime": 1410341734, "pathSuffix": "inputrc",
      "modificationTime": 1410341367, "replication": 1,
      "length": 1021, "owner": "osv", "type": "FILE"
    },
    {
      "group": "osv", "permission": "664", "blockSize": 512,
      "accessTime": 1410341732, "pathSuffix": "hosts",
      "modificationTime": 1410341369, "replication": 1,
      "length": 30, "owner": "osv", "type": "FILE"
    }
  ]
]]

t.run = function()
  osv_request_mock(file_stat_response, {"file", "/etc/hosts"}, "GET", {op = "GETFILESTATUS"})
  osv_request_mock(dir_stat_response, {"file", "/etc"}, "GET", {op = "GETFILESTATUS"})
  osv_request_mock(dir_list_response, {"file", "/etc"}, "GET", {op = "LISTSTATUS"})

  cwd.set("/etc")

  -- List cwd
  assert(t_main({}) ==
    "./      ../     fstab   hosts   inputrc mnttab \n")

  -- List argument
  assert(t_main({"/etc"}) ==
    "./      ../     fstab   hosts   inputrc mnttab \n")

  -- List a file
  assert(t_main({"/etc/hosts"}) ==
    "/etc/hosts\n")

  -- Long list a directory
  assert(t_main({"-l", "/etc"}) == table.concat({"total 6",
  "drwxrwxrwx 4  osv osv 10   Sep 10 12:29 .       ",
  "drwxrwxrwx 11 osv osv 18   Sep 10 12:35 ..      ",
  "-rwxrwxrwx 1  osv osv 141  Sep 10 12:29 fstab   ",
  "-rwxrwxrwx 1  osv osv 30   Sep 10 12:29 hosts   ",
  "-rwxrwxrwx 1  osv osv 1021 Sep 10 12:29 inputrc ",
  "-rwxrwxrwx 0  osv osv 0    Jan 01 1970  mnttab  ", ""}, "\n"))

  -- List reverse order
  assert(t_main({"-r", "/etc"}) ==
    "mnttab  inputrc hosts   fstab   ../     ./     \n")

  -- List by time
  assert(t_main({"-t", "/etc"}) ==
    "../     ./      fstab   hosts   inputrc mnttab \n")

  -- Reverse order by time
  assert(t_main({"-rt", "/etc"}) ==
    "mnttab  inputrc fstab   ./      hosts   ../    \n")

  -- Long list reverse order
  assert(t_main({"-lr", "/etc"}) == table.concat({"total 6",
  "-rwxrwxrwx 0  osv osv 0    Jan 01 1970  mnttab  ",
  "-rwxrwxrwx 1  osv osv 1021 Sep 10 12:29 inputrc ",
  "-rwxrwxrwx 1  osv osv 30   Sep 10 12:29 hosts   ",
  "-rwxrwxrwx 1  osv osv 141  Sep 10 12:29 fstab   ",
  "drwxrwxrwx 11 osv osv 18   Sep 10 12:35 ..      ",
  "drwxrwxrwx 4  osv osv 10   Sep 10 12:29 .       ", ""}, "\n"))

  -- Long list by time
  assert(t_main({"-lt", "/etc"}) == table.concat({"total 6",
"drwxrwxrwx 11 osv osv 18   Sep 10 12:35 ..      ",
"drwxrwxrwx 4  osv osv 10   Sep 10 12:29 .       ",
"-rwxrwxrwx 1  osv osv 141  Sep 10 12:29 fstab   ",
"-rwxrwxrwx 1  osv osv 30   Sep 10 12:29 hosts   ",
"-rwxrwxrwx 1  osv osv 1021 Sep 10 12:29 inputrc ",
"-rwxrwxrwx 0  osv osv 0    Jan 01 1970  mnttab  ", ""}, "\n"))

  -- Long list reverse order by time
  assert(t_main({"-lrt", "/etc"}) == table.concat({"total 6",
"-rwxrwxrwx 0  osv osv 0    Jan 01 1970  mnttab  ",
"-rwxrwxrwx 1  osv osv 1021 Sep 10 12:29 inputrc ",
"-rwxrwxrwx 1  osv osv 141  Sep 10 12:29 fstab   ",
"drwxrwxrwx 4  osv osv 10   Sep 10 12:29 .       ",
"-rwxrwxrwx 1  osv osv 30   Sep 10 12:29 hosts   ",
"drwxrwxrwx 11 osv osv 18   Sep 10 12:35 ..      ", ""}, "\n"))

  -- Long list multiple arguments
  assert(t_main({"-l", "/etc", "/etc/hosts"}) == table.concat({
"-rwxrwxrwx 1 osv osv 30 Sep 10 12:29 /etc/hosts ",
"",
"/etc:",
"total 6",
"drwxrwxrwx 4  osv osv 10   Sep 10 12:29 .       ",
"drwxrwxrwx 11 osv osv 18   Sep 10 12:35 ..      ",
"-rwxrwxrwx 1  osv osv 141  Sep 10 12:29 fstab   ",
"-rwxrwxrwx 1  osv osv 30   Sep 10 12:29 hosts   ",
"-rwxrwxrwx 1  osv osv 1021 Sep 10 12:29 inputrc ",
"-rwxrwxrwx 0  osv osv 0    Jan 01 1970  mnttab  ", ""}, "\n"))
end

return t
