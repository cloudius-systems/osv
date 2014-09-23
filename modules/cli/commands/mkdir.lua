local OptionParser = require('std.optparse')

local cmd = {}

cmd.desc = [[make directories]]

cmd.parser = OptionParser [[
mkdir - make directories

Usage: mkdir [OPTION]... DIRECTORY...

Create the DIRECTORY(ies).

Options:

  -p, --parents  make parent directories as needed
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  for i = 1, #args do
    osv_request({"file", args[i]}, "PUT", {
      op = "MKDIRS",
      permission = "0755", -- TODO: umask?
      createParent = (opts.parents and "true" or "false")
    })
  end
end

return cmd
