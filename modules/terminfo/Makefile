OUT=out
TI_VT100=$(OUT)/terminfo/v/vt100-qemu

module: $(TI_VT100)

$(TI_VT100): $(MAIN)
	@mkdir -p $(OUT)/terminfo
	infocmp vt100 | sed 's/cud1=\(^J\|\\n\),/cud1=\\E[B,/; s/ind=\(^J\|\\n\),/ind=\\E[S,/; s/vt100/vt100-qemu/' \
		> $(OUT)/terminfo.tmp
	tic -o $(OUT)/terminfo $(OUT)/terminfo.tmp
	rm -f $(OUT)/terminfo.tmp

clean:
	rm -rf $(OUT)

.PHONY: module clean
