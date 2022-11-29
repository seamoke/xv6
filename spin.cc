#include<bits/stdc++.h>
int main() {
  int a=1,b=2;
  int *p1 = &a;
  int *p2 = p1;
  p1=&b;
  printf("%d %d\n",*p1,*p2);
  /* int pid;
  char c;
  pid = fork();
  if (pid == 0) {
    c = '/';
  } else {
    c = '\\';
  }
  for (int i = 0;; i++) {
    if ((i % 1000000) == 0)
      write(1, &c, 1);
  } */
}