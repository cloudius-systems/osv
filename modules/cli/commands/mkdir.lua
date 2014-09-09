local OptionParser = require('std.optparse')

local cmd = {}

cmd.parser = OptionParser [[
mkdir

Usage: mkdir [OPTION]... DIRECTORY...

Create the DIRECTORY(ies).

Options:

  -p, --parents  make parent directories as needed
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  for i = optind, #args do
    osv_request({"file", args[i]}, "PUT", {
      op = "MKDIRS",
      permission = "0755", -- TODO: umask?
      createParent = (opts.p and "true" or "false")
    })
  end
end

return cmd
