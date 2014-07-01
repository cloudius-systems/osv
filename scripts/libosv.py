#!/usr/bin/python3
import sys
import re

elf = re.compile(r"\s*?\d+:\s*([0-9a-f]+)\s+\d+\s+(FUNC|OBJECT)\s+GLOBAL\s+DEFAULT\s+\d+\s+(\w+)\s*")

f = open(sys.argv[1])
out = open(sys.argv[2], "w")

asm_type = { "FUNC" : "function", "OBJECT" : "object" }

for line in f.readlines():
    try:
        value,tp,sym = elf.match(line).groups()
        out.write("%s = 0x%s;\n"%(sym, value))
        print (".global %s\n.type %s,@%s"%(sym, sym, asm_type[tp]))
    except AttributeError:
        pass
