void sysfatal(char*, ...);
void *emalloc(ulong);
void *erealloc(void*, ulong);
sockaddr_in *mkinetsa(char*, int);
int listentcp(int);
int bindudp(int);
int acceptcall(int, char*, int);
