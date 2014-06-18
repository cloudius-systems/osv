# You will die horribly without -mstrict-align, due to
# unaligned access to a stack attr variable with stp.
# Relaxing alignment checks via sctlr_el1 A bit setting should solve
# but it doesn't - setting ignored?
#
# Also, mixed TLS models resulted in different var addresses seen by
# different objects depending on the TLS model used.
# Force all __thread variables encountered to local exec.
arch-cflags = -mstrict-align -mtls-dialect=desc -ftls-model=local-exec -DAARCH64_PORT_STUB
