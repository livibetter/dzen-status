/***************************************************************************/
/* System status monitor with dzen2                                        */
/* Written in 2010-2012, 2014, 2016-2017 by Yu-Jie Lin                     */
/*                                                                         */
/* This is free and unencumbered software released into the public domain. */
/*                                                                         */
/* Anyone is free to copy, modify, publish, use, compile, sell, or         */
/* distribute this software, either in source code form or as a compiled   */
/* binary, for any purpose, commercial or non-commercial, and by any       */
/* means.                                                                  */
/*                                                                         */
/* In jurisdictions that recognize copyright laws, the author or authors   */
/* of this software dedicate any and all copyright interest in the         */
/* software to the public domain. We make this dedication for the benefit  */
/* of the public at large and to the detriment of our heirs and            */
/* successors. We intend this dedication to be an overt act of             */
/* relinquishment in perpetuity of all present and future rights to this   */
/* software under copyright law.                                           */
/*                                                                         */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,         */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF      */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  */
/* IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR       */
/* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,   */
/* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR   */
/* OTHER DEALINGS IN THE SOFTWARE.                                         */
/*                                                                         */
/* For more information, please refer to <http://unlicense.org/>           */
/***************************************************************************/

#include <alloca.h>
#include <locale.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>

#define DSCPY(...) strcpy(dzen_str, __VA_ARGS__);
#define DSSPF(...) sprintf(dzen_str, __VA_ARGS__);
#define DSCAT(...) sprintf(dzen_str + strlen(dzen_str), __VA_ARGS__);

#define ERROPENSCAN(path, n, ...)\
  {\
    FILE *f;\
    if ((f = fopen((path), "r")))\
    {\
      if ((n) != fscanf(f, __VA_ARGS__))\
      {\
        fclose(f);\
        goto err;\
      }\
    }\
    else\
    {\
      goto err;\
    }\
    fclose(f);\
  }
#define ERRSCAN(f, n, ...)\
  if ((n) != fscanf(f, __VA_ARGS__))\
  {\
    fclose(f);\
    goto err;\
  }\

#define SLEEP 100000

// resources at critical level, highlighting in white background
#define CRITICAL_CPU 95
#define CRITICAL_MEM 75
#define CRITICAL_THM 60
#define CRITICAL_BAT 10

// On my laptop, /sys/class/power_supply/BAT0/... update interval is 15 seconds
// Normal update interval when capacity is more than low capacity
#define UI_BAT 5000000
// Flashing rate when in low capacity, the default is 500ms for red, 500ms for yellow/cyan
#define UI_BAT_FLASH 500000

FILE *dzen;
uint64_t *update_ts;
char *old_dzen;
char *new_dzen;
char **tmp_dzen;
snd_mixer_t *h_mixer;

struct update_func
{
  uint32_t interval;
  size_t bufsize;
  void (*fp) (int);
};
void update_cpu(int);
void update_mem(int);
void update_fs(int);
void update_net(int);
void update_thm(int);
void update_bat(int);
void update_sound(int);
void update_clock(int);
struct update_func update_funcs[] =
{
  { 1000000, 26, &update_cpu},
  { 5000000, 31, &update_mem},
  {60000000, 21, &update_fs},
  { 5000000, 39, &update_net},
  {10000000, 25, &update_thm},
  {  UI_BAT, 35, &update_bat},
  {  200000, 17, &update_sound},
  { 1000000, 39, &update_clock}
};
const int UPDATE_FUNCS = sizeof(update_funcs) / sizeof(struct update_func);

char *
used_color(int v, int max, int color_max, int min)
{
  static char result[8];

  max = max == -1 ? 100 : max;
  min = min == -1 ?   0 : min;
  v = MAX(MIN(v, max), min);

  color_max = color_max == -1 ? 176 : color_max;
  v = color_max - (v - min) * color_max / (max - min);

  sprintf(result, "#%02x%02x%02x", color_max, v, v);
  return result;
}

void
update_cpu(int ID)
{
  char *dzen_str = tmp_dzen[ID];
  static int ocpu_total = 0;
  static int ocpu_idle = 0;
  FILE *f = fopen("/proc/stat", "r");

  ERRSCAN(f, 0, "%*s");

  int ncpu_total = 0;
  int ncpu_idle = 0;
  for (int i = 0; i < 10; i++)
  {
    int n;
    ERRSCAN(f, 1, "%d", &n);
    ncpu_total += n;
    if (i == 3)
    {
      ncpu_idle = n;
    }
  }
  fclose(f);

  int cpu_maxval, cpu_val, cpu_percentage;
  cpu_maxval = ncpu_total - ocpu_total;
  if (cpu_maxval == 0)
  {
    // skip this round
    return;
  }
  cpu_val = cpu_maxval - (ncpu_idle - ocpu_idle);
  cpu_percentage = 100 * cpu_val / cpu_maxval;

  ocpu_idle = ncpu_idle;
  ocpu_total = ncpu_total;

  *dzen_str = 0;
  if (cpu_percentage >= CRITICAL_CPU)
  {
    DSCPY("^bg(#fff)");
  }

  char *color = used_color(cpu_percentage, 75, -1, 10);
  DSSPF("^fg(%s)%3d%%", color, cpu_percentage);
  return;
err:
  DSCPY("^fg(#fff)^bg(#f00)!!!%%");
}

void
update_mem(int ID)
{
  char *dzen_str = tmp_dzen[ID];
  FILE *f = fopen("/proc/meminfo", "r");

  int total, free, buffers, cached, used;
  char key[32];
  ERRSCAN(f, 1, "%*s %d %*s", &total);
  ERRSCAN(f, 1, "%*s %d %*s", &free);
  ERRSCAN(f, 2, "%s %d %*s", key, &buffers);
  if (strstr(key, "MemAvailable"))
  {
    // the buffers are actually avaiable
    used = total - buffers;
  }
  else
  {
    ERRSCAN(f, 1, "%*s %d %*s", &cached);
    free += buffers + cached;
    used = total - free;
  }
  fclose(f);

  *dzen_str = 0;
  int mem_percentage = 100 * used / total;
  if (mem_percentage >= CRITICAL_MEM)
  {
    DSCPY("^bg(#fff)");
  }
  char *color = used_color(used, 1024 * 1024, -1, 100 * 1024);
  DSCAT("^fg(%s)%4dM%3d%%", color, used / 1024, mem_percentage);
  return;
err:
  DSCPY("^fg(#fff)^bg(#f00)!!!!M!!!%%");
}

void
update_fs(int ID)
{
  char *dzen_str = tmp_dzen[ID];

  struct statvfs root_fs;
  statvfs("/", &root_fs);

  int used, total, percentage;
  const size_t GiB = 1024 * 1024 * 1024;
  used = (root_fs.f_blocks - root_fs.f_bfree) * root_fs.f_bsize / GiB;
  total = root_fs.f_blocks * root_fs.f_bsize / GiB;
  percentage = 100 * used / total;

  char *color = used_color(percentage, 60, -1, 10);
  DSSPF("^fg(%s)%3dG%3d%%", color, used, percentage);
}

void
update_net(int ID)
{
  char *dzen_str = tmp_dzen[ID];
  static uint64_t o_rxb, o_txb;
  static bool nodata = true;

  FILE *f;
  if (!(f = fopen("/sys/class/net/ppp0/statistics/rx_bytes", "r")))
  {
    nodata = true;
    goto nodata;
  }

  uint64_t n_rxb, n_txb, rx_rate, tx_rate;
  ERRSCAN(f, 1, "%ld", &n_rxb);
  fclose(f);
  if (!(f = fopen("/sys/class/net/ppp0/statistics/tx_bytes", "r")))
  {
    nodata = true;
    goto nodata;
  }
  ERRSCAN(f, 1, "%ld", &n_txb);
  fclose(f);

  if (n_rxb < o_rxb || n_txb < o_txb)
  {
    DSCPY("^fg(#a00)???^fg()K^fg(#a00)????");
    goto n2o;
  }

  // rate in bytes
  rx_rate = 1000000 * (n_rxb - o_rxb) / update_funcs[ID].interval;
  tx_rate = 1000000 * (n_txb - o_txb) / update_funcs[ID].interval;

  if (nodata)
  {
    nodata = false;
nodata:
    DSCPY("^fg(#a00)---^fg()K^fg(#a00)----");
    goto n2o;
  }

  // to Kbytes
  rx_rate /= 1024;
  tx_rate /= 1024;

  DSSPF("^fg(%s)%3ld^fg()K", used_color(tx_rate, 200, -1, -1), tx_rate);
  DSCAT("^fg(%s)%4ld", used_color(rx_rate, 500, -1, -1), rx_rate);
n2o:
  o_rxb = n_rxb;
  o_txb = n_txb;
  return;
err:
  DSCPY("^fg(#fff)^bg(f00)!!!K!!!!");
}

void
update_thm(int ID)
{
  char *dzen_str = tmp_dzen[ID];

  FILE *f;
  int thm;
  if ((f = fopen("/sys/class/thermal/thermal_zone0/temp", "r")))
  {
    ERRSCAN(f, 1, "%d", &thm);
    thm /= 1000;
  }
  else
  {
    DSCPY("^fg(#a00)--C");
    return;
  }
  fclose(f);

  *dzen_str = 0;
  if (thm >= CRITICAL_THM)
  {
    DSCPY("^bg(#fff)");
  }

  DSCAT("^fg(%s)%2dC", used_color(thm, 70, -1, 40), thm);
  return;
err:
  DSCPY("^fg(#fff)^bg(#f00)!!C");
}

#define SYSBAT0 "/sys/class/power_supply/BAT0"
void
update_bat(int ID)
{
  char *dzen_str = tmp_dzen[ID];
  static char flashed = 0;

  int full, remaining = 0, percentage;
  ERROPENSCAN(SYSBAT0 "/charge_full", 1, "%d", &full);
  ERROPENSCAN(SYSBAT0 "/charge_now", 1,"%d", &remaining)
  remaining = MIN(remaining, full);
  percentage = 100 * remaining / full;

  // green  = charged
  // yellow = discharing
  // blue   = charging,
  // red    = unknown
  char state[32] = "";
  ERROPENSCAN(SYSBAT0 "/status", 1, "%s", state);
  if (!strcmp(state, "Full"))
  {
    DSCPY("^fg(#0a0)");
    percentage = 100;
  }
  else if (!strcmp(state, "Charging"))
  {
    DSCPY("^fg(#0aa)");
  }
  else if (!strcmp(state, "Discharging"))
  {
    DSCPY("^fg(#aa0)");
  }
  else
  {
    DSCPY("^fg(#a00)");
  }

  update_funcs[ID].interval = UI_BAT;
  if (percentage < CRITICAL_BAT)
  {
    update_funcs[ID].interval = UI_BAT_FLASH;
    // white background flash = capacity is low
    if (flashed)
    {
      DSCAT("^bg(#fff)");
    }
    flashed = !flashed;
  }
  char *color = used_color(100 - percentage, -1, -1, -1);
  DSCAT("^fg(%s)%3d%%", color, percentage);
  return;
err:
  DSCPY("^fg(#fff)^bg(#f00)!!!%%");
}

void
update_sound(int ID)
{
  char *dzen_str = tmp_dzen[ID];
  // https://github.com/livibetter-backup/yjl/blob/master/Miscellaneous/get-volume.c
  const char *ATTACH = "default";
  const snd_mixer_selem_channel_id_t CHANNEL = SND_MIXER_SCHN_FRONT_LEFT;
  const char *SELEM_NAME = "Master";
  long vol, vol_min, vol_max;
  int switch_value;

  static snd_mixer_selem_id_t *sid = NULL;
  static snd_mixer_elem_t *elem = NULL;

  if (!elem)
  {
    snd_mixer_open(&h_mixer, 1);
    snd_mixer_attach(h_mixer, ATTACH);
    snd_mixer_selem_register(h_mixer, NULL, NULL);
    snd_mixer_load(h_mixer);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, SELEM_NAME);

    elem = snd_mixer_find_selem(h_mixer, sid);
  }

  snd_mixer_handle_events(h_mixer);
  snd_mixer_selem_get_playback_volume(elem, CHANNEL, &vol);
  snd_mixer_selem_get_playback_volume_range(elem, &vol_min, &vol_max);
  snd_mixer_selem_get_playback_switch(elem, CHANNEL, &switch_value);

  int percentage = 100 * vol / vol_max;

  if (switch_value)
  {
    DSSPF("^fg(#%02xaaaa)", 176 - percentage * 176 / 100)
  }
  else
  {
    DSCPY("^fg(#a00)");
  }
  DSCAT("%3d%%", percentage);
}

void
update_clock(int ID)
{
  char *dzen_str = tmp_dzen[ID];

  time_t t = time(NULL);
  struct tm *tmp = localtime(&t);
  const char *TIMEFMT = "%A, %B %d, %Y %H:%M:%S";
  const size_t bufsize = update_funcs[ID].bufsize;
  size_t s = bufsize - (strftime(dzen_str, bufsize, TIMEFMT, tmp) + 1);

  if (s)
  {
    memset(dzen_str, ' ', s);
    strftime(dzen_str + s, bufsize - s, TIMEFMT, tmp);
  }
}

void
update_next_ts(int ID)
{
  struct timeval t;
  gettimeofday(&t, NULL);

  update_ts[ID] = t.tv_sec * 1000000 + t.tv_usec + update_funcs[ID].interval;
}

void
clean_up()
{
  snd_mixer_close(h_mixer);
  free(old_dzen);
  free(new_dzen);
  for (int i = 0; i < UPDATE_FUNCS; i++)
  {
    free(tmp_dzen[i]);
  }
  free(tmp_dzen);
  free(update_ts);
  pclose(dzen);
}

void
sig_handler(int sig)
{
  (void) sig;

  clean_up();
  signal(sig, SIG_DFL);
  raise(sig);
}

int
main(void)
{
  int i;

  // http://www.cl.cam.ac.uk/~mgk25/unicode.html#c
  if (!setlocale(LC_CTYPE, ""))
  {
    fprintf(stderr,
            "Can't set the specified locale! Check LANG, LC_CTYPE, LC_ALL.\n");
    return EXIT_FAILURE;
  }

  signal(SIGINT, sig_handler);
  signal(SIGKILL, sig_handler);
  signal(SIGTERM, sig_handler);

  // spawn dzen2
  char dzen_conf[256];
  const char *p = getenv("XDG_CONFIG_HOME");
  if (p)
  {
    strcpy(dzen_conf, p);
  }
  else
  {
    strcat(strcpy(dzen_conf, getenv("HOME")), "/.config");
  }
  strcat(dzen_conf, "/dzen-status/dzen");

  char dzen_cmd[256];
  if (!(dzen = fopen(dzen_conf, "r")) ||
      !fgets(dzen_cmd, sizeof(dzen_cmd), dzen))
  {
    fprintf(stderr, "cannot read %s.\n", dzen_conf);
    return EXIT_FAILURE;
  }
  fclose(dzen);
  if (!(dzen = popen(dzen_cmd, "w")))
  {
    fprintf(stderr, "cannot open dzen2 with:\n  %s.\n", dzen_cmd);
    return EXIT_FAILURE;
  }

  // initialize buffers
  update_ts = (uint64_t *) malloc(UPDATE_FUNCS * sizeof(uint64_t));
  tmp_dzen  = (char **)    malloc(UPDATE_FUNCS * sizeof(char *));
  // allbufsize = sum of bufsizes (N * '\0' => (N - 1) * '|' + '\0')
  //            + (N - 1) * '^fg()^^bg()'
  size_t allbufsize = (UPDATE_FUNCS - 1) * 10;
  for (i = 0; i < UPDATE_FUNCS; i++)
  {
    allbufsize += update_funcs[i].bufsize;
    tmp_dzen[i] = (char *) malloc(update_funcs[i].bufsize);
    update_funcs[i].fp(i);
  }

  char *old_dzen = (char *) malloc(allbufsize);
  char *new_dzen = (char *) malloc(allbufsize);
  const struct timespec req = {.tv_sec = 0, .tv_nsec = SLEEP * 1000};
  for (;;)
  {
    uint64_t ts_current;
    struct timeval t;

    gettimeofday(&t, NULL);
    ts_current = t.tv_sec * 1000000 + t.tv_usec;

    for (i = 0; i < UPDATE_FUNCS; i++)
    {
      if (ts_current >= update_ts[i])
      {
        update_funcs[i].fp(i);
        update_next_ts(i);
      }
    }

    strcpy(new_dzen, tmp_dzen[0]);
    for (i = 1; i < UPDATE_FUNCS; i++)
    {
      strcat(new_dzen, "^fg()^bg()|");
      strcat(new_dzen, tmp_dzen[i]);
    }

    if (strcmp(old_dzen, new_dzen))
    {
      fprintf(dzen, "%s\n", new_dzen);
      fflush(dzen);
      strcpy(old_dzen, new_dzen);
    }
    nanosleep(&req, NULL);
  }

  clean_up();
  return EXIT_SUCCESS;
}
