#ifndef __PERF_PAGER_H
#define __PERF_PAGER_H

extern void pager_init(const char *pager_env);

extern void setup_pager(void);
extern int pager_in_use(void);

#endif /* __PERF_PAGER_H */
