local OptionParser = require("std.optparse")

local rwx
local format_file
local sort_files

local flag_l = false
local flag_t = false
local flag_r = false

local year_before = 0
local year_after = 0

local cmd = {}

cmd.desc = [[list directory contents]]

cmd.parser = OptionParser [[
ls - list directory contents

Usage: ls [OPTION]... [FILE]...

List information  about the FILEs (the current directory by default).

Options:

  -a, --all      do not ignore entries starting with .
  -l             use a long listing format
  -r, --reverse  reverse order while sorting
  -t             sort by modification time, newest first
      --help     display this help and exit
]]

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  -- Flags
  flag_l = opts.l
  flag_t = opts.t
  flag_r = opts.reverse

  -- Date boundary for long listing
  local dtmp = os.date("*t")
  dtmp.month = dtmp.month - 6
  year_before = os.time(dtmp)
  dtmp.year = dtmp.year + 1
  year_after = os.time(dtmp)

  -- No arguments means cwd
  if #args == 0 then
    table.insert(args, cwd.get())
  end

  -- Filter all the erroneous arguments, saving the stat
  local rargs = {}
  for _, arg in ipairs(args) do
    local content, status = osv_request({"file", cwd.resolve(arg)}, "GET", {op = "GETFILESTATUS"})
    osv_resp_assert(status, 200, 404, 400)

    if status == 404 then
      io.stderr:write("ls: ", arg, ": no such file or directory\n")
    elseif status == 400 then
      io.stderr:write("ls: ", arg, ": failed to fetch data\n")
    else
      table.insert(rargs, {file=content, arg=arg})
    end
  end

  -- Counter to separate sections with blank lines
  local print_counter = 0

  -- Sort the arguments
  table.sort(rargs, function(a, b)
    return sort_files(a.file, b.file)
  end)

  -- List non-directory arguments
  local file_list = {}
  for arg_i, arg in ipairs(rargs) do
    local file, path = arg.file, arg.arg

    if file.type ~= "DIRECTORY" then
      print_counter = print_counter + 1
      file.pathSuffix = path
      table.insert(file_list, format_file(file, flag_l))
    end
  end

  if #file_list > 0 then
    -- Print the files
    if flag_l then
      io.write(table_format(file_list), '\n')
    else
      io.write(list_format(file_list), '\n')
    end

    -- Empty line if we have more arguments
    if print_counter < #rargs then
      io.write('\n')
    end
  end

  -- List the directory arguments
  for rarg_i, rarg in ipairs(rargs) do
    local file, rpath = rarg.file, cwd.resolve(rarg.arg)

    if file.type == "DIRECTORY" then
      print_counter = print_counter + 1

      local content, status = osv_request({"file", rpath}, "GET", {op = "LISTSTATUS"})
      osv_resp_assert(status, 200)

      -- Sort
      table.sort(content, sort_files)

      -- Build the output list
      local file_list = {}
      local total_blocks = 0
      for _, file in ipairs(content) do
        total_blocks = total_blocks + math.ceil(file.length / file.blockSize)
        table.insert(file_list, format_file(file, flag_l))
      end

      -- Print label of current folder listing
      if #rargs > 1 then
        io.write(rarg.arg, ":", "\n")
      end

      -- Format accordingly
      if flag_l then
        io.write("total ", tostring(total_blocks), "\n")
        io.write(table_format(file_list), "\n")
      else
        io.write(list_format(file_list), "\n")
      end

      -- Empty line after folder listing
      if print_counter < #rargs then
        io.write("\n")
      end
    end
  end
end

format_file = function(file, longformat)
  local tdate = os.date("%b%d%Y%H:%M", file.modificationTime)

  local time_field = string.sub(tdate, 10)
  if file.modificationTime < year_before or
     file.modificationTime > year_after then
    time_field = string.sub(tdate, 6, 9)
  end

  if longformat then
    return {
      rwx(file),
      tostring(file.replication),
      file.owner,
      file.group,
      tostring(file.length),
      string.sub(tdate, 1, 3),
      string.sub(tdate, 4, 5),
      time_field,
      file.pathSuffix
    }
  else
    return file.pathSuffix .. (file.type == "DIRECTORY" and "/" or "")
  end
end

sort_files = function(a, b)
  local a_cmp, b_cmp

  if flag_r then
    a_cmp, b_cmp = b, a
  else
    a_cmp, b_cmp = a, b
  end

  if flag_t then
    return a_cmp.modificationTime > b_cmp.modificationTime
  else
    return string.lower(a_cmp.pathSuffix) < string.lower(b_cmp.pathSuffix)
  end
end

local mods = "rwx"
rwx = function(file)
  local ret = {}
  if file.type == "DIRECTORY" then
    table.insert(ret, 'd')
  else
    table.insert(ret, '-')
  end

  local mod = file.permission
  for i = 1, #mod do
    for j = 1, #mods do
      if bit32.band(tonumber(string.sub(mod, i, i)), j) then
        table.insert(ret, string.sub(mods, j, j))
      else
        table.insert(ret, '-')
      end
    end
  end

  return table.concat(ret)
end

return cmd
