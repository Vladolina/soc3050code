/*
 * sys.h - what Main.c (the tasks) and shell.c (the commands) share
 *
 * Everything here is owned by Main.c.  The shell only READS the counters
 * and only REQUESTS changes (prof_request_clear, the fault masks) - one
 * writer per variable, the rule this whole template is built on.  Two
 * exceptions, both on purpose: tel_quiet has the shell as its ONLY writer,
 * and kbd has two (the shell feeds it, input applies it), so kbd_lock -
 * as does mirror.c's state (shell: $MIRROR; display: $FB), so mirror_lock.
 */
#ifndef SYS_H
#define SYS_H

#include <stddef.h>
#include <stdint.h>
#include "os.h"
#include "kbd.h"
#include "health.h"
#include "prof.h"

/* The watched tasks.  Index = health counter = prof[] slot = fault-mask bit. */
enum { T_INPUT = 0, T_APP, T_DISPLAY, T_TEL, T_WATCHED };

extern os_mutex_t print_lock;    /* whole lines on the serial port          */
extern os_mutex_t bus_lock;      /* I2C1: the OLED, and anything you add    */
extern os_mutex_t app_lock;      /* the app's state: step vs draw vs tel    */
extern os_mutex_t kbd_lock;      /* kbd: shell's kbd_frame vs input's apply */
extern os_mutex_t mirror_lock;   /* mirror.c: shell's $MIRROR vs display    */

extern health_t   health;
extern prof_t     prof[T_WATCHED];

extern volatile uint32_t fault_hang;   /* bit i: task i blocks forever       */
extern volatile uint32_t fault_spin;   /* bit i: task i spins forever        */
extern volatile uint32_t i2c_errors;
extern uint8_t           oled_ok;      /* written once, before os_start()    */
extern uint32_t          boot_flags;   /* RCC->CSR2 as found at reset        */
extern kbd_t             kbd;          /* the station's keys, under kbd_lock */
extern volatile uint8_t  tel_quiet;    /* `quiet`/`loud`: only the shell writes */

void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void send_body(const char *body, int len);     /* $BODY*CS\n */
void send_raw(const char *buf, size_t len);    /* a ready-made line, no printf */

/* The station's keys (kbd.h), as the banner and the '?' key print them. */
#define KEYS_HELP "  keys     : arrows = stick, a = A (brake), b/p = B (restart), s = SEL," \
                  " 1-5 = knob, ? = this list\n"
void print_keys(void);                         /* shell.c: say(KEYS_HELP)     */

void task_shell(void *arg);                    /* shell.c */

#endif
