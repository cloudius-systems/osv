#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <histedit.h>

#include <sys/ioctl.h>
#include <termios.h>

#ifdef OSV_CLI
#define CLI_LUA "/cli/cli.lua"
#define CLI_LUA_PATH "/usr/share/lua/5.2/?.lua;/cli/lib/?.lua;/cli/lua/share/?.lua"
#define CLI_LUA_CPATH "/usr/lib/lua/5.2/?.so;/cli/lua/lib/?.so"
#define CLI_COMMANDS_PATH "/cli/commands"
#else
#define CLI_LUA "cli.lua"
#define CLI_LUA_PATH "lib/?.lua;../lua/out/share/lua/5.2/?.lua"
#define CLI_LUA_CPATH "../lua/out/lib/lua/5.2/?.so"
#define CLI_COMMANDS_PATH "./commands"
#endif

#define PROMPT_MAXLEN 128
static char sprompt[PROMPT_MAXLEN];

/* Console size handling */
static int con_height = 24;
static int con_width = 80;
void cli_sigwinch_handler(int);
void cli_console_size_dirty();

/* Report console size back to Lua */
static int cli_lua_console_dim(lua_State *);

static  lua_State *L;
void      luaL_renew_cli(lua_State**);
lua_State *luaL_newstate_cli();
char      *prompt(EditLine*);

/* Context */
static char* env_osv_api;

int main (int argc, char* argv[]) {
#ifdef OSV_CLI
  putenv("TERM=vt100-qemu");

  cli_console_size_dirty();
#else
  struct winsize sz;
  ioctl(0, TIOCGWINSZ, &sz);

  if (sz.ws_col > 0 && sz.ws_row > 0) {
    con_width = sz.ws_col;
    con_height = sz.ws_row;

    signal(SIGWINCH, cli_sigwinch_handler);
  }
#endif

  /* Context */
  env_osv_api = getenv("OSV_API");

  /* Process command line options */
  int opt = 0;
  static struct option long_options[] = {
    {"api", optional_argument, 0, 'a'}
  };

  int long_index = 0;
  while ((opt = getopt_long(argc, argv, "a:",
    long_options, &long_index)) != -1) {
    switch (opt) {
      case 'a': env_osv_api = optarg;
      break;
    }
  }

  /* This holds all the state for our line editor */
  EditLine *el;

  /* This holds the info for our history */
  History *cli_history;

  /* Temp variables */
  int keepreading = 1;
  HistEvent ev;

  /* editline */
  int el_count = 0;
  char *el_line;

  /* Initialize the EditLine state to use our prompt function and
  emacs style editing. */
  el = el_init(argv[0], stdin, stdout, stderr);
  el_set(el, EL_PROMPT, &prompt);
  el_set(el, EL_SIGNAL, 1);
  el_set(el, EL_EDITOR, "emacs");

  /* Initialize the history */
  cli_history = history_init();
  if (cli_history == 0) {
    fprintf(stderr, "history could not be initialized\n");
  }

  /* Set the size of the history */
  history(cli_history, &ev, H_SETSIZE, 800);

  /* This sets up the call back functions for history functionality */
  el_set(el, EL_HIST, history, cli_history);

  /* Lua state */
  L = luaL_newstate_cli();

  while (keepreading) {
    /* el_count is the number of characters read.
       line is a const char* of our command line with the tailing \n */
    el_line = (char *) el_gets(el, &el_count);

    /* If lua failed to load previously, retry */
    if (L == NULL) {
      luaL_renew_cli(&L);
    }

    /* with zero input (^D), reset the lua state */
    if (L != NULL && el_count == 0) {
      luaL_renew_cli(&L);
    } else if (L != NULL && el_count > 0) {
      /* Remove tailing \n */
      el_line[strlen(el_line)-1] = '\0';

      /* Add commands to history. Don't add empty lines */
      if (strlen(el_line) > 0) {
        history(cli_history, &ev, H_ENTER, el_line);
      }

      /* Typing reset is a special case which, like ^D,
         will reset the lua state */
      if (strcmp(el_line, "reset") == 0) {
        luaL_renew_cli(&L);
      } else {
        /* Pass the line, as is, to cli() */
        lua_getglobal(L, "cli");
        lua_pushstring(L, el_line);

        int error = lua_pcall(L, 1, 0, 0);
        if (error) {
          fprintf(stderr, "%s\n", lua_tostring(L, -1));
          lua_pop(L, 1);
        }
      }
    }
  }

  if (L != NULL) {
    lua_close(L);
  }

  history_end(cli_history);
  el_end(el);

  return 0;
}

void luaL_renew_cli(lua_State **L) {
  fprintf(stderr, "\nRestarting shell\n");
  if (*L != NULL) {
    lua_close(*L);
  }

  *L = luaL_newstate_cli();
}

void cli_lua_settable(lua_State *L, char *table, char *key, const char *value) {
  lua_getglobal(L, table);
  lua_pushstring(L, key);
  lua_pushstring(L, value);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

lua_State *luaL_newstate_cli() {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  cli_lua_settable(L, "package", "path", CLI_LUA_PATH);
  cli_lua_settable(L, "package", "cpath", CLI_LUA_CPATH);

  int error = luaL_loadfile(L, CLI_LUA) || lua_pcall(L, 0, 0, 0);
  if (error) {
    fprintf(stderr, "Failed to load shell: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    lua_close(L);

    return NULL;
  }

  cli_lua_settable(L, "context", "commands_path", CLI_COMMANDS_PATH);
  if (env_osv_api) {
    cli_lua_settable(L, "context", "api", env_osv_api);
  }

  /* Bind some functions into Lua */
  lua_pushcfunction(L, cli_lua_console_dim);
  lua_setglobal(L, "cli_console_dim");

  return L;
}

char *prompt(EditLine *e) {
  /* Get the shell prompt from Lua */
  lua_getglobal(L, "prompt");
  int error = lua_pcall(L, 0, 1, 0);
  if (error) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
  } else {
    if (!lua_isnil(L, -1)) {
      const char *lprompt = lua_tostring(L, -1);
      int len = strlen(lprompt);
      snprintf(sprompt, len < PROMPT_MAXLEN ? len+1 : PROMPT_MAXLEN, "%s", lprompt);
      lua_pop(L, 1);
      return sprompt;
    }
  }

  /* Default to an empty prompt in case of an error */
  return (char *)"# ";
}

/* Signal handler for SIGWINCH (change in window size) */
void cli_sigwinch_handler(int sig) {
  struct winsize sz;
  ioctl(0, TIOCGWINSZ, &sz);
  con_width = sz.ws_col;
  con_height = sz.ws_row;
}

/* Uses some ANSI sequences in raw mode to find the window size */
void cli_console_size_dirty() {
  /* Switch to raw and save current */
  struct termios t_prev, t_raw;
  tcgetattr(0, &t_prev);
  cfmakeraw(&t_raw);
  tcsetattr(0, TCSANOW, &t_raw);

  /* Get current cursor location */
  printf("\033[6n");
  int cur_h, cur_w;
  int res = scanf("\033[%d;%dR", &cur_h, &cur_w);
  if (!res) {
    goto giveup;
  }

  /* Set cursor location to 999x999 and query it again */
  printf("\033[999;999H\033[6n");
  int width, height;
  res = scanf("\033[%d;%dR", &height, &width);
  if (res) {
    con_height = height;
    con_width = width;
  }

  /* Return the cursor to its original location */
  printf("\033[%d;%dH", cur_h, cur_w);

giveup:
  tcsetattr(0, TCSANOW, &t_prev);
}

/* Returns console size to Lua */
static int cli_lua_console_dim(lua_State *L) {
  lua_pushnumber(L, con_height);
  lua_pushnumber(L, con_width);
  return 2;
}
