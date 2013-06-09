#ifndef AF_LOCAL_H_
#define AF_LOCAL_H_

#ifdef __cplusplus
extern "C" {
#endif

int socketpair_af_local(int type, int proto, int sv[2]);

int shutdown_af_local(int fd, int how);

#ifdef __cplusplus
}
#endif

#endif /* AF_LOCAL_H_ */
