struct fault_fixup {
    ulong pc;
    ulong divert;
    friend bool operator<(fault_fixup a, fault_fixup b) {
        return a.pc < b.pc;
    }
};

extern fault_fixup fault_fixup_start[], fault_fixup_end[];

static void sort_fault_fixup() __attribute__((constructor(init_prio::sort)));

static void sort_fault_fixup()
{
    std::sort(fault_fixup_start, fault_fixup_end);
}


