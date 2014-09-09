local OptionParser = require('std.optparse')

local cmd = {}

cmd.parser = OptionParser [[
echo - display a line of text

Usage: echo [OPTION]... [STRING]...

Echo the STRING(s) to standard output.

Options:

  -n          do not output the trailing newline
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)
  io.write(table.concat(args, " ") .. (opts.n and "" or "\n"))
end

return cmd
