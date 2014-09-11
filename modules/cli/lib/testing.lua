function command_test_filename(name)
  return string.format('%s/%s_test.lua', context.commands_test_path, name)
end

local stdout_io = nil
local stderr_io = nil
local stdout_orig = nil
local stderr_orig = nil

--- Redirects stdout and stderr to temp files
local function t_start_buffers()
  stdout_orig = io.output()
  stdout_io = io.tmpfile()
  io.output(stdout_io)

  stderr_io = io.tmpfile()
  stderr_orig, io.stderr = io.stderr, stderr_io
end

--- Closes the temp files, restores stdout and stderr and returns the buffers
local function t_end_buffers()
  io.output(stdout_orig)
  io.stderr = stderr_orig

  stdout_io:seek("set")
  stderr_io:seek("set")

  local out, err = stdout_io:read("*all"), stderr_io:read("*all")

  stdout_io:close()
  stderr_io:close()

  return out, err
end

--- Runs the test subject and returns the recorded stdout and stderr
local t_module = nil
function t_main(args)
  t_start_buffers()
  if t_module then
    t_module.main(args)
  end
  return t_end_buffers()
end

--- Sets the test subject
function t_subject(m)
  t_module = m
end
