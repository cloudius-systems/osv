local long_opts = {
  parents = 'p'
}

local cmd = {}

cmd.main = function(args)
  local do_parents = "false"

  local optarg, optind = alt_getopt.get_opts(args, "p", {})
  assert(optarg, optind)

  for k, v in pairs(optarg) do
    if k == 'p' then
      do_parents = "true"
    end
  end

  for i = optind, #args do
    osv_request({"file", args[i]}, "PUT", {
      op = "MKDIRS",
      permission = "0755", -- TODO: umask?
      createParent = do_parents
    })
  end
end

return cmd
