#include <stdint.h>
#include <string.h>
#include <elf.h>

#define ETC_LDSO_PATH "/etc/ld-musl-x86_64.path"

#define IS_COPY(x) ((x)==R_X86_64_COPY)
#define IS_PLT(x) ((x)==R_X86_64_JUMP_SLOT)

static inline void do_single_reloc(
	struct dso *self, unsigned char *base_addr,
	size_t *reloc_addr, int type, size_t addend,
	Sym *sym, size_t sym_size,
	struct symdef def, size_t sym_val)
{
	switch(type) {
	case R_X86_64_GLOB_DAT:
	case R_X86_64_JUMP_SLOT:
	case R_X86_64_64:
		*reloc_addr = sym_val + addend;
		break;
	case R_X86_64_32:
		*(uint32_t *)reloc_addr = sym_val + addend;
		break;
	case R_X86_64_PC32:
		*reloc_addr = sym_val + addend - (size_t)reloc_addr + (size_t)base_addr;
		break;
	case R_X86_64_RELATIVE:
		*reloc_addr = (size_t)base_addr + addend;
		break;
	case R_X86_64_COPY:
		memcpy(reloc_addr, (void *)sym_val, sym_size);
		break;
	case R_X86_64_DTPMOD64:
		*reloc_addr = def.dso ? def.dso->tls_id : self->tls_id;
		break;
	case R_X86_64_DTPOFF64:
		*reloc_addr = def.sym->st_value + addend;
		break;
	case R_X86_64_TPOFF64:
		*reloc_addr = (def.sym
			? def.sym->st_value - def.dso->tls_offset
			: 0 - self->tls_offset) + addend;
		break;
	}
}
