local cmd = {}

cmd.main = function(args)
  local optarg, optind = alt_getopt.get_opts(args, "", {})
  assert(optarg, optind)

  for i = optind, #args do
    osv_request({"file", args[i]}, "DELETE", {op = "DELETE"})
  end
end

return cmd
