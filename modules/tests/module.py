from osv.modules import api
import os

if os.getenv('ARCH') == 'x64':
    api.require('java-tests')
api.require('dl_tests')
