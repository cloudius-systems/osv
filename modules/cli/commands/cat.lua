local cmd = {}

cmd.main = function(args)
  local optarg, optind = alt_getopt.get_opts(args, "", {})
  assert(optarg, optind)

  for i = optind, #args do
    local path, rpath = args[i], cwd.resolve(args[i])
    local content, status = osv_request({"file", rpath}, "GET", {op = "GET"}, true)
    osv_resp_assert(status, 200, 404)

    if status == 404 then
      io.stderr:write(path .. ": File not found" .. "\n")
    else
      print(content)
    end
  end
end

return cmd
