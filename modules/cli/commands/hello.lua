--[[
Commands are expected to be Lua modules.
This file is an example of such a module.
For more on Lua modules, see: http://lua-users.org/wiki/ModulesTutorial
]]--

local cmd = {}

--- Called when the command is executed
-- @param args List of arguments from the command line
cmd.main = function(args)
	print("Hello, OSv!")
end

return cmd
