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

-- Print matrix to console
function table_print(tt)
  -- Find maximum length of each column
  local tlength = {}

  for i = 1, #tt do
    for j = 1, #tt[i] do
      tlength[j] = math.max(tlength[j] or 0, #tt[i][j])
    end
  end

  for i = 1, #tt do
    local p = ""
    for j = 1, #tt[i] do
      local pad = -(tlength[j] + 1)
      p = p .. string.format("%-" .. (pad) .. "s", tt[i][j], tlength[j])
    end
    print(p)
  end
end

function file_exists(name)
  local f = io.open(name, "r")
  if f ~= nil then io.close(f) return true else return false end
end
