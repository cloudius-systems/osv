local cmd = {}

cmd.desc = [[close shell and exit]]

cmd.main = function()
	print("Goodbye")
	os.exit()
end

return cmd
