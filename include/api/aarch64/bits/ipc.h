#ifndef BITS_IPC_H
#define BITS_IPC_H

struct ipc_perm {
    key_t __ipc_perm_key;
    uid_t uid;
    gid_t gid;
    uid_t cuid;
    gid_t cgid;
    unsigned short int mode;
    unsigned short int __pad1;
    unsigned short int __ipc_perm_seq;
    unsigned short int __pad2;
    unsigned long long __unused1;
    unsigned long long __unused2;
};

#define IPC_64 0

#endif /* BITS_IPC_H */
