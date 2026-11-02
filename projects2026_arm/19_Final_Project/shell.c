/*
 * shell.c - the health shell: what is the system doing, and is it OK?
 *
 *   help              the command list
 *   stats             every task: priority, CPU %, stack high-water mark,
 *                     response time avg/max, worst lateness, overruns.
 *                     Then zeroes the timing counters, so two `stats` a few
 *                     seconds apart measure exactly the interval between them.
 *   health            heartbeat counters, watchdog feeds, reset cause
 *   scan              I2C bus scan, under bus_lock (the display shares it)
 *   hang <task>       make a task block forever - a deadlock, simulated
 *   spin <task>       make a task loop forever  - a runaway, simulated
 *   reset             NVIC_SystemReset()
 *   quiet / loud      stop / restart the periodic $APP and $SYS frames, so a
 *                     person at a terminal can read the shell's answers
 *
 * Lines that start with '$' are not commands but FRAMES from the station
 * (tools/station/station.py), and are answered with silence:
 *
 *   $KEY,<key>,<1|0>*CS   a key went down or up -> kbd_frame() (kbd.h)
 *   $MIRROR,<1|0>*CS      "keep sending the OLED" -> mirror_frame() (mirror.h)
 *
 * The station sends a $KEY for every key press and release and a $MIRROR
 * every second: one word of reply to each would bury the console.  The only
 * key that prints is '?', which asks for the key list.
 *
 * The CPU column is the kernel's own accounting (os.h: `ticks` counts the
 * 1 ms ticks during which each task was the one running).  It is a SAMPLE:
 * a task that runs 200 us per wake-up and happens never to be running when
 * SysTick fires reads 0 %.  Over a few seconds the sampling evens out; the
 * response-time columns, from TIM14, are exact.
 */
#include <stdio.h>
#include <string.h>
#include "stm32c031xx.h"
#include "os.h"
#include "uart.h"
#include "i2c.h"
#include "oled.h"
#include "wdog.h"
#include "kbd.h"
#include "mirror.h"
#include "sys.h"

static uint32_t prev_ticks[OS_MAX_TASKS], prev_now;

static void cmd_stats(void)
{
    uint32_t now = os_ticks(), span = now - prev_now;
    if (span == 0u) { span = 1u; }
    say("over the last %lu ms:\n", (unsigned long)span);
    say("task     pri  cpu%%   stack used/size   period  runs  resp avg/max us  late  over\n");
    for (uint32_t i = 0; i < os_task_count(); i++) {
        os_task_t *t = os_task(i);
        uint32_t pm = (t->ticks - prev_ticks[i]) * 1000u / span;    /* per mille */
        prev_ticks[i] = t->ticks;
        const prof_t *p = 0;
        for (uint32_t k = 0; k < T_WATCHED; k++) {
            if (strcmp(prof[k].name, t->name) == 0) { p = &prof[k]; }
        }
        if (p && p->runs) {
            say("%-8s %3u %3lu.%lu %5lu/%-5lu words %4lu ms %5lu %6lu/%-6u %4u ms %4lu\n",
                t->name, (unsigned)t->base_prio, (unsigned long)(pm / 10u), (unsigned long)(pm % 10u),
                (unsigned long)os_stack_used(t), (unsigned long)t->stack_words,
                (unsigned long)p->period_ms, (unsigned long)p->runs,
                (unsigned long)(p->resp_sum_us / p->runs), (unsigned)p->resp_max_us,
                (unsigned)p->late_max_ms, (unsigned long)p->overruns);
        } else {
            say("%-8s %3u %3lu.%lu %5lu/%-5lu words\n",
                t->name, (unsigned)t->base_prio, (unsigned long)(pm / 10u), (unsigned long)(pm % 10u),
                (unsigned long)os_stack_used(t), (unsigned long)t->stack_words);
        }
    }
    const volatile uart_stats_t *u = uart_stats();
    say("switches %lu, i2c errors %lu, oled %lu bytes in %lu flushes, uart tx waits %lu, rx dropped %lu\n",
        (unsigned long)os_switches(), (unsigned long)i2c_errors,
        (unsigned long)oled_bytes_sent(), (unsigned long)oled_flushes(),
        (unsigned long)u->tx_waits, (unsigned long)u->rx_dropped);
    for (uint32_t k = 0; k < T_WATCHED; k++) { prof_request_clear(&prof[k]); }
    prev_now = now;
}

static void cmd_health(void)
{
    say("heartbeats:");
    for (uint32_t k = 0; k < T_WATCHED; k++) {
        say(" %s=%lu", prof[k].name, (unsigned long)health.beats[k]);
    }
    say("\nwatchdog fed %lu times, %lu checks starved, missing now 0x%02lX\n",
        (unsigned long)health.feeds, (unsigned long)health.starved_checks,
        (unsigned long)health.missing);
    say("last reset: %s (CSR2 0x%08lX)\n", wdog_reset_cause(boot_flags),
        (unsigned long)boot_flags);
}

static void cmd_scan(void)
{
    char line[96];
    int n = snprintf(line, sizeof line, "I2C:");
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        os_mutex_lock(&bus_lock);
        int rc = i2c_probe(a);
        os_mutex_unlock(&bus_lock);
        if (rc == I2C_OK && n < (int)sizeof line - 6) {
            n += snprintf(line + n, sizeof line - (size_t)n, " 0x%02X", a);
        } else if (rc == I2C_TIMEOUT) {
            say("I2C: timeout at 0x%02X - bus stuck?\n", a);
            return;
        }
    }
    say("%s%s\n", line, n == 4 ? " nothing answered" : "");
}

/* "hang display" -> sets bit T_DISPLAY in *mask.  0 if the name is unknown. */
static int inject(volatile uint32_t *mask, const char *name, const char *what)
{
    for (uint32_t k = 0; k < T_WATCHED; k++) {
        if (strcmp(prof[k].name, name) == 0) {
            say("%s: %s - watch `health`, LD4 and the screen\n", what, name);
            *mask |= 1u << k;
            return 1;
        }
    }
    say("tasks: input app display telem\n");
    return 0;
}

void print_keys(void) { say("%s", KEYS_HELP); }

/* A line from the station.  Returns 1 if it was one of its frames - handled,
 * and never answered - or 0 to let the command parser see it as before. */
static int station_frame(const char *line)
{
    if (kbd_is_key(line)) {
        os_mutex_lock(&kbd_lock);            /* input's kbd_apply() reads kbd */
        int cmd = kbd_frame(&kbd, line, os_ticks());
        os_mutex_unlock(&kbd_lock);          /* print, if at all, unlocked    */
        if (cmd == KBD_HELP) { print_keys(); }
        /* KBD_SLOWER / KBD_FASTER ('-' and '+') mean nothing in this template
         * and are dropped without a word - steer has no pace to change.  Give
         * them a meaning in your project here (lesson 12: the game's speed).
         * A $KEY with a bad checksum comes back KBD_NONE: also silence. */
        return 1;
    }
    os_mutex_lock(&mirror_lock);             /* display's mirror_next() too   */
    int m = mirror_frame(line, os_ticks());
    os_mutex_unlock(&mirror_lock);
    return m;
}

void task_shell(void *arg)
{
    (void)arg;
    char line[32];        /* >= KBD_LINE_MAX (24): the longest $KEY line fits */
    uint32_t n = 0;
    for (;;) {
        char c = (char)uart_getc();                  /* blocks: no CPU used */
        if (c != '\r' && c != '\n') { if (n < sizeof line - 1u) { line[n++] = c; } continue; }
        if (n == 0) { continue; }
        line[n] = '\0';
        n = 0;
        if (line[0] == '$' && station_frame(line)) { continue; }
        if      (strcmp(line, "stats")  == 0) { cmd_stats(); }
        else if (strcmp(line, "health") == 0) { cmd_health(); }
        else if (strcmp(line, "scan")   == 0) { cmd_scan(); }
        else if (strncmp(line, "hang ", 5) == 0) { inject(&fault_hang, line + 5, "hang"); }
        else if (strncmp(line, "spin ", 5) == 0) { inject(&fault_spin, line + 5, "spin"); }
        else if (strcmp(line, "quiet")  == 0) { tel_quiet = 1; say("quiet: no $APP/$SYS - type loud to resume\n"); }
        else if (strcmp(line, "loud")   == 0) { tel_quiet = 0; say("loud: $APP and $SYS on\n"); }
        else if (strcmp(line, "reset")  == 0) {
            say("reset\n");
            os_delay(20);
            NVIC_SystemReset();
        } else {
            say("commands: stats  health  scan  hang <task>  spin <task>  reset  quiet  loud\n");
        }
    }
}
