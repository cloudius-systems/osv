-- TODO: Print sizes in different format

local cmd = {}

cmd.main = function(args)
  local optarg, optind = alt_getopt.get_opts(args, "", {})
  assert(optarg, optind)

  local path = args[optind]
  local df, status = osv_request({"fs", "df", path}, "GET")
  osv_resp_assert(status, 200, 404)

  if status == 404 then
    io.stderr:write(path .. ": Mount point not found\n")
  else
    local tt = {{"Filesystem", "Total", "Used", "Use%", "Mounted on"}}
    for i = 1, #df do
      local fs = df[i]
      table.insert(tt, {
        fs.filesystem,
        tostring(fs.btotal),
        tostring(fs.btotal - fs.bfree),
        string.format("%.2f%%", 100 - ((fs.bfree / fs.btotal) * 100)),
        fs.mount
      })
    end

    print(table_format(tt))
  end
end

return cmd
