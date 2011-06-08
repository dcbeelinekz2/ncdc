/* ncdc - NCurses Direct Connect client

  Copyright (c) 2011 Yoran Heling

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/


#include "ncdc.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <sys/file.h>


#if INTERFACE

// Get a string from a glib log level
#define loglevel_to_str(level) (\
  (level) & G_LOG_LEVEL_ERROR    ? "ERROR"    :\
  (level) & G_LOG_LEVEL_CRITICAL ? "CRITICAL" :\
  (level) & G_LOG_LEVEL_WARNING  ? "WARNING"  :\
  (level) & G_LOG_LEVEL_MESSAGE  ? "message"  :\
  (level) & G_LOG_LEVEL_INFO     ? "info"     : "debug")

// number of columns of a gunichar
#define gunichar_width(x) (g_unichar_iswide(x) ? 2 : g_unichar_iszerowidth(x) ? 0 : 1)


#endif





// Configuration handling


// global vars
const char *conf_dir;
GKeyFile *conf_file;


#if INTERFACE

#define conf_hub_get(type, name, key) (\
  g_key_file_has_key(conf_file, name, (key), NULL)\
    ? g_key_file_get_##type(conf_file, name, (key), NULL)\
    : g_key_file_get_##type(conf_file, "global", (key), NULL))

#define conf_autorefresh() (\
  !g_key_file_has_key(conf_file, "global", "autorefresh", NULL) ? 60\
    : g_key_file_get_integer(conf_file, "global", "autorefresh", NULL))

#define conf_slots() (\
  !g_key_file_has_key(conf_file, "global", "slots", NULL) ? 10\
    : g_key_file_get_integer(conf_file, "global", "slots", NULL))

#endif


void conf_init() {
  // get location of the configuration directory
  conf_dir = g_getenv("NCDC_DIR");
  if(!conf_dir)
    conf_dir = g_build_filename(g_get_home_dir(), ".ncdc", NULL);

  // try to create it (ignoring errors if it already exists)
  g_mkdir(conf_dir, 0700);
  if(g_access(conf_dir, F_OK | R_OK | X_OK | W_OK) < 0)
    g_error("Directory '%s' does not exist or is not writable.", conf_dir);

  // we should also have a logs/ subdirectory
  char *logs = g_build_filename(conf_dir, "logs", NULL);
  g_mkdir(logs, 0777);
  if(g_access(conf_dir, F_OK | R_OK | X_OK | W_OK) < 0)
    g_error("Directory '%s' does not exist or is not writable.", logs);
  g_free(logs);

  // make sure that there is no other ncdc instance working with the same config directory
  char *ver_file = g_build_filename(conf_dir, "version", NULL);
  int ver_fd = g_open(ver_file, O_WRONLY|O_CREAT, 0600);
  if(ver_fd < 0 || flock(ver_fd, LOCK_EX|LOCK_NB))
    g_error("Unable to open lock file. Is another instance of ncdc running with the same configuration directory?");

  // check data directory version
  // version = major, minor
  //   minor = forward & backward compatible, major only backward.
  char dir_ver[2] = {1, 0};
  if(read(ver_fd, dir_ver, 2) < 2)
    if(write(ver_fd, dir_ver, 2) < 2)
      g_error("Could not write to '%s': %s", ver_file, g_strerror(errno));
  g_free(ver_file);
  // Don't close the above file. Keep it open and let the OS close it (and free
  // the lock) when ncdc is closed, was killed or has crashed.
  if(dir_ver[0] > 1)
    g_error("Incompatible data directory. Please upgrade ncdc or use a different directory.");

  // load config file (or create it)
  conf_file = g_key_file_new();
  char *cf = g_build_filename(conf_dir, "config.ini", NULL);
  GError *err = NULL;
  if(g_file_test(cf, G_FILE_TEST_EXISTS)) {
    if(!g_key_file_load_from_file(conf_file, cf, G_KEY_FILE_KEEP_COMMENTS, &err))
      g_error("Could not load '%s': %s", cf, err->message);
  }
  // always set the initial comment
  g_key_file_set_comment(conf_file, NULL, NULL,
    "This file is automatically managed by ncdc.\n"
    "While you could edit it yourself, doing so is highly discouraged.\n"
    "It is better to use the respective commands to change something.\n"
    "Warning: Editing this file while ncdc is running may result in your changes getting lost!", NULL);
  // make sure a nick is set
  if(!g_key_file_has_key(conf_file, "global", "nick", NULL)) {
    char *nick = g_strdup_printf("ncdc_%d", g_random_int_range(1, 9999));
    g_key_file_set_string(conf_file, "global", "nick", nick);
    g_free(nick);
  }
  conf_save();
  g_free(cf);
}


void conf_save() {
  char *dat = g_key_file_to_data(conf_file, NULL, NULL);
  char *cf = g_build_filename(conf_dir, "config.ini", NULL);
  FILE *f = fopen(cf, "w");
  if(!f || fputs(dat, f) < 0 || fclose(f))
    g_critical("Cannot save config file '%s': %s", cf, g_strerror(errno));
  g_free(dat);
  g_free(cf);
}





/* A best-effort character conversion function.
 *
 * If, for whatever reason, a character could not be converted, a question mark
 * will be inserted instead. Unlike g_convert_with_fallback(), this function
 * does not fail on invalid byte sequences in the input string, either. Those
 * will simply be replaced with question marks as well.
 *
 * The character sets in 'to' and 'from' are assumed to form a valid conversion
 * according to your iconv implementation.
 *
 * Modifying this function to not require glib, but instead use the iconv and
 * memory allocation functions provided by your system, should be trivial.
 *
 * This function does not correctly handle character sets that may use zeroes
 * in the middle of a string (e.g. UTF-16).
 *
 * This function may not represent best practice with respect to character set
 * conversion, nor has it been thoroughly tested.
 */
char *str_convert(const char *to, const char *from, const char *str) {
  GIConv cd = g_iconv_open(to, from);
  if(cd == (GIConv)-1) {
    g_critical("No conversion from '%s' to '%s': %s", from, to, g_strerror(errno));
    return g_strdup("<encoding-error>");
  }
  gsize inlen = strlen(str);
  gsize outlen = inlen+96;
  gsize outsize = inlen+100;
  char *inbuf = (char *)str;
  char *dest = g_malloc(outsize);
  char *outbuf = dest;
  while(inlen > 0) {
    gsize r = g_iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
    if(r != (gsize)-1)
      continue;
    if(errno == E2BIG) {
      gsize used = outsize - outlen - 4;
      outlen += outsize;
      outsize += outsize;
      dest = g_realloc(dest, outsize);
      outbuf = dest + used;
    } else if(errno == EILSEQ || errno == EINVAL) {
      // skip this byte from the input
      inbuf++;
      inlen--;
      // Only output question mark if we happen to have enough space, otherwise
      // it's too much of a hassle...  (In most (all?) cases we do have enough
      // space, otherwise we'd have gotten E2BIG anyway)
      if(outlen >= 1) {
        *outbuf = '?';
        outbuf++;
        outlen--;
      }
    } else
      g_assert_not_reached();
  }
  memset(outbuf, 0, 4);
  g_iconv_close(cd);
  return dest;
}


// Test that conversion is possible from UTF-8 to fmt and backwards.  Not a
// very comprehensive test, but ensures str_convert() can do its job.
// The reason for this test is to make sure the conversion *exists*,
// whether it makes sense or not can't easily be determined. Note that my
// code currently can't handle zeroes in encoded strings, which is why this
// is also tested (though, again, not comprehensive. But at least it does
// not allow UTF-16)
// Returns FALSE if the encoding can't be used, optionally setting err when it
// has something useful to say.
gboolean str_convert_check(const char *fmt, GError **err) {
  GError *l_err = NULL;
  gsize read, written, written2;
  char *enc = g_convert("abc", -1, "UTF-8", fmt, &read, &written, &l_err);
  if(l_err) {
    g_propagate_error(err, l_err);
    return FALSE;
  } else if(!enc || read != 3 || strlen(enc) != written) {
    g_free(enc);
    return FALSE;
  } else {
    char *dec = g_convert(enc, written, fmt, "UTF-8", &read, &written2, &l_err);
    g_free(enc);
    if(l_err) {
      g_propagate_error(err, l_err);
      return FALSE;
    } else if(!dec || read != written || written2 != 3 || strcmp(dec, "abc") != 0) {
      g_free(dec);
      return FALSE;
    } else {
      g_free(dec);
      return TRUE;
    }
  }
}


// Number of columns required to represent the UTF-8 string.
int str_columns(const char *str) {
  int w = 0;
  while(*str) {
    w += gunichar_width(g_utf8_get_char(str));
    str = g_utf8_next_char(str);
  }
  return w;
}


// returns the byte offset to the last character in str (UTF-8) that does not
// fit within col columns.
int str_offset_from_columns(const char *str, int col) {
  const char *ostr = str;
  int w = 0;
  while(*str && w < col) {
    w += gunichar_width(g_utf8_get_char(str));
    str = g_utf8_next_char(str);
  }
  return str-ostr;
}


// Stolen from ncdu (with small modifications)
// Result is stored in an internal buffer.
char *str_formatsize(guint64 size) {
  static char dat[11]; /* "xxx.xx MiB" */
  double r = size;
  char c = ' ';
  if(r < 1000.0f)      { }
  else if(r < 1023e3f) { c = 'k'; r/=1024.0f; }
  else if(r < 1023e6f) { c = 'M'; r/=1048576.0f; }
  else if(r < 1023e9f) { c = 'G'; r/=1073741824.0f; }
  else if(r < 1023e12f){ c = 'T'; r/=1099511627776.0f; }
  else                 { c = 'P'; r/=1125899906842624.0f; }
  sprintf(dat, "%6.2f %c%cB", r, c, c == ' ' ? ' ' : 'i');
  return dat;
}


// case-insensitive substring match
char *str_casestr(const char *haystack, const char *needle) {
  gsize hlen = strlen(haystack);
  gsize nlen = strlen(needle);

  while(hlen-- >= nlen) {
    if(!g_strncasecmp(haystack, needle, nlen))
      return (char*)haystack;
    haystack++;
  }
  return NULL;
}


// Prefixes all strings in the array-of-strings with a string, obtained by
// concatenating all arguments together. Last argument must be NULL.
void strv_prefix(char **arr, const char *str, ...) {
  // create the prefix
  va_list va;
  va_start(va, str);
  char *prefix = g_strdup(str);
  const char *c;
  while((c = va_arg(va, const char *))) {
    char *o = prefix;
    prefix = g_strconcat(prefix, c, NULL);
    g_free(o);
  }
  va_end(va);
  // add the prefix to every string
  char **a;
  for(a=arr; *a; a++) {
    char *o = *a;
    *a = g_strconcat(prefix, *a, NULL);
    g_free(o);
  }
  g_free(prefix);
}



// Split a two-argument string into the two arguments.  The first argument
// should be shell-escaped, the second shouldn't. The string should be
// writable. *first should be free()'d, *second refers to a location in str.
void str_arg2_split(char *str, char **first, char **second) {
  GError *err = NULL;
  while(*str == ' ')
    str++;
  char *sep = str;
  gboolean bs = FALSE;
  *first = *second = NULL;
  do {
    if(err)
      g_error_free(err);
    err = NULL;
    sep = strchr(sep+1, ' ');
    if(sep && *(sep-1) == '\\')
      bs = TRUE;
    else {
      if(sep)
        *sep = 0;
      *first = g_shell_unquote(str, &err);
      if(sep)
        *sep = ' ';
      bs = FALSE;
    }
  } while(sep && (err || bs));
  if(sep && sep != str) {
    *second = sep+1;
    while(**second == ' ')
      (*second)++;
  }
}



// like realpath(), but also expands ~
char *path_expand(const char *path) {
  char *p = path[0] == '~' ? g_build_filename(g_get_home_dir(), path+1, NULL) : g_strdup(path);
  char *r = realpath(p, NULL);
  g_free(p);
  return r;
}



// String pointer comparison, for use with qsort() on string arrays.
int cmpstringp(const void *p1, const void *p2) {
  return strcmp(* (char * const *) p1, * (char * const *) p2);
}

// Expand and auto-complete a filesystem path
void path_suggest(char *opath, char **sug) {
  char *path = g_strdup(opath);
  char *name, *dir = NULL;

  // special-case ~ and .
  if((path[0] == '~' || path[0] == '.') && (path[1] == 0 || (path[1] == '/' && path[2] == 0))) {
    name = path_expand(path);
    sug[0] = g_strconcat(name, "/", NULL);
    g_free(name);
    goto path_suggest_f;
  }

  char *sep = strrchr(path, '/');
  if(sep) {
    *sep = 0;
    name = sep+1;
    dir = path_expand(path[0] ? path : "/");
    if(!dir)
      goto path_suggest_f;
  } else {
    name = path;
    dir = path_expand(".");
  }
  GError *err = NULL;
  GDir *d = g_dir_open(dir, 0, &err);
  if(!d) {
    g_error_free(err);
    goto path_suggest_f;
  }

  const char *n;
  int i = 0, len = strlen(name);
  while(i<20 && (n = g_dir_read_name(d))) {
    if(strcmp(n, ".") == 0 || strcmp(n, "..") == 0)
      continue;
    char *fn = g_build_filename(dir, n, NULL);
    if(strncmp(n, name, len) == 0 && strlen(n) != len)
      sug[i++] = g_file_test(fn, G_FILE_TEST_IS_DIR) ? g_strconcat(fn, "/", NULL) : g_strdup(fn);
    g_free(fn);
  }
  g_dir_close(d);
  qsort(sug, i, sizeof(char *), cmpstringp);

path_suggest_f:
  g_free(path);
  if(dir)
    free(dir);
}




// from[24] (binary) -> to[39] (ascii - no padding zero will be added)
void base32_encode(const char *from, char *to) {
  static char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  int i, bits = 0, idx = 0, value = 0;
  for(i=0; i<24; i++) {
    value = (value << 8) | (unsigned char)from[i];
    bits += 8;
    while(bits > 5) {
      to[idx++] = alphabet[(value >> (bits-5)) & 0x1F];
      bits -= 5;
    }
  }
  if(bits > 0)
    to[idx++] = alphabet[(value << (5-bits)) & 0x1F];
}


// from[39] (ascii) -> to[24] (binary)
void base32_decode(const char *from, char *to) {
  int i, bits = 0, idx = 0, value = 0;
  for(i=0; i<39; i++) {
    value = (value << 5) | (from[i] <= '9' ? (26+(from[i]-'2')) : from[i]-'A');
    bits += 5;
    while(bits > 8) {
      to[idx++] = (value >> (bits-8)) & 0xFF;
      bits -= 8;
    }
  }
}





// Transfer / hashing rate calculation

/* How to use this:
 * From main thread:
 *   struct ratecalc thing;
 *   ratecalc_init(&thing);
 *   ratecalc_register(&thing);
 * From any thread (usually some worker thread):
 *   ratecalc_add(&thing, bytes);
 * From main thread:
 *   rate = ratecalc_get(&thing);
 *   ratecalc_reset(&thing);
 *   ratecalc_unregister(&thing);
 *
 * ratecalc_calc() should be called with a one-second interval
 */

#if INTERFACE

struct ratecalc {
  int counter;
  int rate;
  char isreg;
};

#define ratecalc_add(rc, b) g_atomic_int_add(&((rc)->counter), b)

#define ratecalc_reset(rc) do {\
    g_atomic_int_set(&((rc)->counter), 0);\
    (rc)->rate = 0;\
  } while(0)

#define ratecalc_init(rc) do {\
    ratecalc_unregister(rc);\
    ratecalc_reset(rc);\
  } while(0)

#define ratecalc_register(rc) do { if(!(rc)->isreg) {\
    ratecalc_list = g_slist_prepend(ratecalc_list, rc);\
    (rc)->isreg = 1;\
  } } while(0)

#define ratecalc_unregister(rc) do {\
    ratecalc_list = g_slist_remove(ratecalc_list, rc);\
    (rc)->isreg = 0;\
  } while(0)

#define ratecalc_get(rc) ((rc)->rate)

#define ratecalc_calc() do {\
    GSList *n; int cur; struct ratecalc *rc;\
    for(n=ratecalc_list; n; n=n->next) {\
      rc = n->data;\
      do {\
        cur = g_atomic_int_get(&(rc->counter));\
      } while(!g_atomic_int_compare_and_exchange(&(rc->counter), cur, 0));\
      rc->rate = cur + ((rc->rate - cur) / 2);\
    }\
  } while(0)

#endif

GSList *ratecalc_list = NULL;

