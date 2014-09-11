local t = {}

t.run = function()
  local out, err

  out, err = t_main({"foo", "bar"})
  assert(out == "foo bar\n")

  out, err = t_main({"-n", "foo"})
  assert(out == "foo")
end

return t
