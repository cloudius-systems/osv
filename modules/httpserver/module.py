from osv.modules import api

api.require('httpserver-html5-gui')

fg = api.run('/libhttpserver-api.so')

# httpserver will run regardless of an explicit command line
# passed with "run.py -e".
daemon = api.run_on_init('/libhttpserver-api.so &!')
default = daemon
