local t = {}

t.run = function()
  local df_response = '[{"mount": "/", "ffree": 20332030, "ftotal": 20332846, "filesystem": "/dev/vblk0.1", "bfree": 20332030, "btotal": 20514542}]'
  osv_request_mock(df_response, {"fs", "df", ""}, "GET")
  osv_request_mock({response="none", status=404}, {"fs", "df", "foo"}, "GET")

  assert(t_main({}) == ("Filesystem   Total    Used   Use%  Mounted on \n" ..
                        "/dev/vblk0.1 20514542 182512 0.89% /          \n"))

  local out, err = t_main({"foo"})
  assert(err == "df: foo: Mount point not found\n")
end

return t
