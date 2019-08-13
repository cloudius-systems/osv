from osv.modules.filemap import FileMap

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/lua/install/lua_modules/').to('/usr') \
	.exclude('lib/luarocks/**') \
	.exclude('share/lua/*/luarocks/**')
