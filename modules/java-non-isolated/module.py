from osv.modules import api

java_base = api.require('java-base')
java_base.non_isolated_jvm = True
provides = ['java-cmd']
