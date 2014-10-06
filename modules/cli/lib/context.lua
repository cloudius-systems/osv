local M = {}

M.commands_path = "commands"
M.commands_test_path = "test"
M.api = "http://127.0.0.1:8000"

M.ssl_key    = nil
M.ssl_cert   = nil
M.ssl_cacert = nil
M.ssl_verify = "peer"

return M
