static __thread int *m0;
int m = 0;

void foo()
{
  m0 = &m;
  *m0 = 14;
}
