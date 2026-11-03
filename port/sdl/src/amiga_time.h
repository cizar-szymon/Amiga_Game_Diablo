#ifndef AMIGA_TIME_H
#define AMIGA_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

unsigned long amiga_now_ms(void);
void amiga_time_close(void);
unsigned long amiga_eclock(void);
unsigned long amiga_eclock_per_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* AMIGA_TIME_H */
