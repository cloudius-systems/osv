#include <stdio.h>
extern int m;
extern void foo();

int main()
{
  foo();
  printf("M = %d\n", m);
  return 0;
}
