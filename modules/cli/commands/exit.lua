local cmd = {}

cmd.desc = [[close shell and exit]]
cmd.help = [[Usage: exit

Exit the shell.]]

cmd.main = function()
	print("Goodbye")
	os.exit()
end

return cmd
