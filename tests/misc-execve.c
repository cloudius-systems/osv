#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <osv/osv_execve.h>

/*
 * Usage: S0 ID_MAX path-to-payload payload-param-1 payload-param-2 ...
 * payload is then called as:
 *   path-to-payload ID payload-param-1 payload-param-2
 */
int main(int argc, char **argv)
{
  int ii, jj;
  int id_max = 10;
  char *prog_filename = NULL; // "/usr/lib/tst-execve-payload.so";
  fprintf(stderr, "TT In tst-execve.c main.\n");
  fprintf(stderr, "TT argc %d\n", argc);
  for (ii=0; ii<argc; ii++) {
    fprintf(stderr, "TT argv[%d] = %s\n", ii, argv[ii]);
  }
  if (argc < 2) {
    fprintf(stderr, "TT Usage: ./tst-execve-payload.so ID_MAX path-to-payload payload-param-1 payload-param-2 ...\n");
    return 1;
  }
  id_max = atoi(argv[1]);
  prog_filename = argv[2];
  if (access(prog_filename, X_OK) != 0) {
    fprintf(stderr, "TT Program '%s' is not executable\n", prog_filename);
    return 1;
  }

  int id;
  char ** prog_argv = NULL;
  int th_id = -1;
  char ** prog_envp = NULL;

  prog_argv = (char**)malloc(1024);
  prog_argv[0] = prog_filename;
  prog_argv[1] = (char*)malloc(1024);  // id goes here
  for (ii=3, jj=2; ii<argc; ii++, jj++) {
    prog_argv[jj] = argv[ii];
  }
  prog_argv[jj] = NULL;

  prog_envp = (char**)malloc(1024);
  prog_envp[0] = "var0=aaa";
  //prog_envp[1] = "var1=bbb";
  prog_envp[1] = "OSV_CPUS=166"; // should replace existing env value
  prog_envp[2] = "var2=ccc";
  prog_envp[3] = (char*)malloc(1024); // id+something goes here
  prog_envp[4] = NULL;

  for (id=0; id<id_max; id++) {
    snprintf(prog_argv[1], 1023, "%d", id);
    snprintf(prog_envp[3], 1023, "MYID=%d", id);

    fprintf(stderr, "TT id=%d before osv_execve\n", id);
    th_id = osv_execve(prog_filename, prog_argv, prog_envp, NULL, -1);
    fprintf(stderr, "TT id=%d osv_execve ret %d\n", id, th_id);
  }

  fprintf(stderr, "TT tst-execve.c main EXIT\n");
  return 0;
}
