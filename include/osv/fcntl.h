#ifndef OSV_FCNTL_H
#define OSV_FCNTL_H

__BEGIN_DECLS

/*
 * Kernel encoding of open mode; separate read and write bits that are
 * independently testable: 1 greater than the above.
 */
#define FREAD           0x00000001
#define FWRITE          0x00000002

/* convert from open() flags to/from fflags; convert O_RD/WR to FREAD/FWRITE */
#define FFLAGS(oflags)  ((oflags) + 1)
#define OFLAGS(fflags)  ((fflags) - 1)

__END_DECLS

#endif /* !OSV_FCNTL_H */
