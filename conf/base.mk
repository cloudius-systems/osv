conf_preempt=1
conf_tracing=0
conf_debug_memory=0

# debug level logging (enabled automatically in mode=debug)
conf_logger_debug=0

conf_debug_elf=0
conf_hide_symbols=0
conf_linker_extra_options=
conf_cxx_level=gnu++14

conf_lazy_stack=0
conf_lazy_stack_invariant=0

# x86_64 PMU (performance-counter) readout, diagnostic-only (see conf/kconfig
# core "config pmu" and include/osv/pmu.hh).  Off by default; set to 1 to build
# in arch/x64/pmu.o and the PMUPROOF boot line.
conf_pmu=0
