from osv.modules.filemap import FileMap

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/terminfo/out/terminfo').to('/usr/share/terminfo')
