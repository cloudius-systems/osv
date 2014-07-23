from osv.modules.filemap import FileMap

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/lua/src/liblua.so').to('/usr/lib/liblua.so')

usr_files.add('${OSV_BASE}/modules/lua/out').to('/usr') \
	.exclude('bin/*') \
	.exclude('etc/*') \
	.exclude('lib/luarocks/**') \
	.exclude('share/lua/*/luarocks/**')
