#!/usr/bin/python
import sys
import re

elf = re.compile(r"\s*?\d+:\s*([0-9a-f]+)\s+\d+\s+(FUNC|OBJECT)\s+(GLOBAL|WEAK)\s+DEFAULT\s+\d+\s+(\w+)\s*")

f = open(sys.argv[1])
out = open(sys.argv[2], "w")
version_str = sys.argv[3]

asm_type = {"FUNC": "function", "OBJECT": "object"}

version = """
.pushsection .note.osv, "a", @note;
        .long 2f-1f;
        .long 4f-3f;
        .long 1;
        .balign 4;
1:
        .asciz "OSv";
2:
        .balign 4;
3:
        .asciz "%s";
4:
	.balign 4;
"""%(version_str)

for line in f.readlines():
    try:
        value, tp, bnd, sym = elf.match(line).groups()
        out.write("%s = 0x%s;\n"%(sym, value))
        print (".global %s\n.type %s,@%s"%(sym, sym, asm_type[tp]))
    except AttributeError:
        pass

print(version)
