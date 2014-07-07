return {
  run = function(args)
    local optarg, optind = alt_getopt.get_opts(args, "al", {})
    assert(optarg,optind)

    local f, err = io.open(args[optind], "r")
    assert(f, err)

    for line in f:lines() do
      print(line)
    end

    f:close()
  end
}
