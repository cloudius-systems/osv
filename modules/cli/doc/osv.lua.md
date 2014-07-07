command: osv
============

`osv` command will translate the REST API definition to a usable command. APIs
and resources/actions should be translated to subcommands. The idea is to have
a main OSv CLI tool that corresponds to the running version so it will be as
forward compatible as possible.

Since this is sort of a RESTful API, actions with side effects (POST, DELETE
etc.) will require using a designated flag. The command will not try to hide
that it's using the API and will be a short-hand human command line interface
thereof.

Help and usage sections will be generated from the API.

Synopsis
--------

`osv [OPTIONS] <api> <action-word [action-word ...]> [parameter=value, ...]`

### api

The API to call. For example: `os` or `jvm`

### action-word

A word that would construct the action. For example: version

### Options

    -h, --help: Display usage according to the command displayed.
    -m, --method: Method for request. Default is GET for *all* actions. It
                  *does not* default to a different method when GET is not
                  available, since non-GET methods will have side-effects.
    -r, --raw: Print the response as is, without further processing.

### Main naming convention

api and action-word will be concatenated with `/` as separator and prepanded
with a `/`. See examples at the bottom.

Process command
---------------

```lua
$ osv
If no arguments or one argument and wrong API requested
  print main schema usage: list of available APIs
  return

$ osv os
If no other arguments or wrong API action requested
  print API usage: list of available actions
  return

$ osv os version
If requested API action exists
  $ osv -h os version
  If -h flag added
    print extended usage: full description, methods, flags, etc.
    return

  $ osv os version
  $ osv -m POST os shutdown
  If -m/--method is <METHOD> or GET
    # Default method is GET.
    # To perform actions with side effects, one must use the proper flags

    If the requested API supports <METHOD>
      verify that params match
      run the command and render the output
    Otherwise
      print extended usage
```

Translate main schema to APIs
-----------------------------

For each API:

* API name: Resource main path on the resource specific JSON. Ex. /os turns to "os"
* API description: API description from the main Schema
* API actions: List of resources from the API specific JSON.

For each action in API:

* Action name: Extract name from path. Ex. /os/version turns to "version"
* Action description: Description field
* Action method: Use with -m POST
* Action parameters: Comes after action-words in the form of "param=value"

Renderers
---------

Since the output is printed to console, we need proper rendering of the return
values. `osv_api.lua` contains a `render_response` function that will render
the output of the call according to the response_class. If one doesn't exist,
it will dump the response as is with a warning of a missing renderer.

This should suffice to provide reasonable forward compatibility, with the added
value of having designated renderers for special types of outputs, like top
or page support.

Reserved commands
-----------------

### set-server, get-server (TODO)

The shell is just a Lua embedded script engine. In that sense, it's possible
running it outside OSv. To set which OSv server you're talking to one can use
`osv set-server` and `osv get-server`.

For example: `osv set-server 192.168.122.89:8000`

Default is: 127.0.0.1:8000

Examples
--------

`osv os version` -> `GET /os/version`

`osv -m POST os shutdown` -> `POST /os/shutdown`
