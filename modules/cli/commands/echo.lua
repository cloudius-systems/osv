local long_opts = {
  help = 0
}

local function usage()
  io.write([[Usage: echo [ [LONG-OPTION] | [SHORT-OPTION]... [STRING]... ]
Display a line of text

-n     do not output the trailing newline
--help display this help and exit
]])
end

local cmd = {}

cmd.main = function(args)
  local line = {}
  local with_newline = true

  local optarg, optind = alt_getopt.get_opts(args, "n", long_opts)
  assert(optarg,optind)

  for k, v in pairs(optarg) do
    if k == 'n' then
      with_newline = false
    end

    if k == 'help' then
      usage()
    end
  end

  for i = optind,#args do
    table.insert(line, args[i])
  end

  io.write(table.concat(line, " ") .. (with_newline and "\n" or ""))
end

return cmd
