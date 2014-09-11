local t = {}

t.run = function()
  local df_response = '[{"mount": "/", "ffree": 20332030, "ftotal": 20332846, "filesystem": "/dev/vblk0.1", "bfree": 20332030, "btotal": 20514542}]'
  osv_request_mock(df_response, {"fs", "df", ""}, "GET")

  local out = t_main({})
  assert(out == ("Filesystem   Total    Used   Use%  Mounted on \n" ..
				 "/dev/vblk0.1 20514542 182512 0.89% /          \n"))
end

return t
