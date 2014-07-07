OSv CLI
=======

OSv requires an internal command line interface that should have very little
dependencies. Namely, not Java.

Editline + Lua
--------------

The shell is based on [editline](http://thrysoee.dk/editline/) and uses Lua to
run the commands. The commands, therefore, will be Lua scripts.

Commands as Lua modules are expected to return a table with a `run` function.
For example:

```
# Sample command
return {
  run = function()
    print("I am a sample command")
  end
}
```

Wrapper
-------

The CLI program is a wrapper around a lua_State. It loads an initial
environment for the scripts (cli.lua) and calls the function `cli(line)` where
line is the command line as it was typed. The lua_State is reused.

This structure lets Lua maintain the state for the environment (variables,
callbacks, etc.) which can be easily cleaned/reset by starting a new lua_State.
Plus, we can create a few functions which are external to the CLI and its
environment for fallbacks when the Lua code crashes or doesn't work.

Command development notes
-------------------------

* Define global functions and variables *only* when necessary. Use `local`
  where possible.
* OSv API utility is available (`lib/osv_api.lua`).
* `getopt_long()`-like is available, see `echo` command for example.
* Use `assert()` as you would normally use in Lua. It is handled properly in
	the embedding part.

Libraries and dependencies
--------------------------

* editline
* lua5.2

### Lua libraries used

* luajson and Lpeg
* LuaSocket

TODO
----

* Autocomplete.
* man and general console help.

Further development tasks and ideas
-----------------------------------

* Exit code - Remember last exit code (or failure/success state).
* Interrupting a command (^C) - I originally wrapped lua_pcall() call with
  sigsetjump() and signal handling. But since OSv is "single-process" I'm not
  sure that's the proper way to go.
* Piping - See [filters, source and sinks](http://lua-users.org/wiki/FiltersSourcesAndSinks) in lua-users.
* SSH - Dropbear seems like the go-to solution in the "light", embedded world.
  Currently, all SSH implementation I saw, except for Apache SSHD, use
  fork/vfork when handling incoming connections, so that will need to be
  addressed.
