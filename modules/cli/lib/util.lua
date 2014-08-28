function trim(s)
  return s:match'^%s*(.*%S)%s*$' or ''
end

function split(str, pattern)
  pattern = pattern or "[^%s]+"
  if pattern:len() == 0 then pattern = "[^%s]+" end
  local parts = {__index = table.insert}
  setmetatable(parts, parts)
  str:gsub(pattern, parts)
  setmetatable(parts, nil)
  parts.__index = nil
  return parts
end

-- Format matrix to be to console
function table_format(tt)
  local tlength, ret = {}, {}

  -- Find maximum length of each column
  for i = 1, #tt do
    for j = 1, #tt[i] do
      tlength[j] = math.max(tlength[j] or 0, #tt[i][j])
    end
  end

  for i = 1, #tt do
    local p = {}
    for j = 1, #tt[i] do
      local pad = -(tlength[j] + 1)
      table.insert(p, string.format("%-" .. (pad) .. "s", tt[i][j], tlength[j]))
    end
    table.insert(ret, table.concat(p, " "))
  end

  return table.concat(ret, "\n")
end

-- Print a list of items
function list_format(t)
  local ret = {}
  local col_pad = 1
  local col_pad_str = string.rep(" ", col_pad)

  -- Find maximum length of items
  local col_length = 0

  for i = 1, #t do
    col_length = math.max(col_length, #t[i])
  end

  local h, w = cli_console_dim()
  local columns = math.floor(w / (col_length + col_pad))

  local buf = {}
  for i = 1, #t do
    table.insert(buf, string.format("%-" .. col_length .. "s", t[i]))
    if #buf == columns then
      table.insert(ret, table.concat(buf, col_pad_str))
      buf = {}
    end
  end

  if #buf > 0 then
    table.insert(ret, table.concat(buf, col_pad_str))
  end

  return table.concat(ret, "\n")
end

function file_exists(name)
  local f = io.open(name, "r")
  if f ~= nil then io.close(f) return true else return false end
end

-- Map with func
function map(func, array)
  local new_array = {}
  for i,v in ipairs(array) do
    new_array[i] = func(v)
  end
  return new_array
end
