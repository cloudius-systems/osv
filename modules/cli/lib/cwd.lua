--- Manages CWD for the shell
local PATH = require("path").new("/")

-- Path defaults to /
local last_path = nil
local current_path = "/"

local cwd = {}

--- Set the current path
cwd.set = function(path)
	last_path, current_path = current_path, path
end

--- Get the current path
cwd.get = function()
	return current_path
end

--- Get the previous path
cwd.last_path = function()
	return last_path
end

--- Resolve a path with the current path
cwd.resolve = function(path)
	if not path then
		return current_path
	end

	return PATH:normpath(PATH:join(current_path, path))
end

return cwd
