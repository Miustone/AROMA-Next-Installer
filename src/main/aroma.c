/*
 * Copyright (C) 2011 Ahmad Amarullah ( http://amarullz.com/ )
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Descriptions:
 * -------------
 * Main executable for AROMA Installer Binary
 *
 */
#include <signal.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include "../aroma.h"

//*
//* GLOBAL UI VARIABLES
//*
static  FILE  * acmd_pipe;
static  int     parent_pid = 0;
static  int     paused_pids[8];
static  int     paused_count = 0;
static  char    currArgv[2][256];
static  byte    reboot_opt = 0;
static  char    reboot_type[32] = {0};

static  char    g_tmp_dir[256]     = "/tmp/aroma";
static  char    g_tmp_s_dir[256]   = "/tmp/aroma-data";
static  char    g_install_log[300] = "/tmp/aroma/.install.log";
static  char    g_install_txt[300] = "/tmp/aroma/.install.txt";

const char * aroma_tmp_dir(void)          { return g_tmp_dir; }
const char * aroma_tmp_s_dir(void)        { return g_tmp_s_dir; }
const char * aroma_install_log_path(void) { return g_install_log; }
const char * aroma_install_txt_path(void) { return g_install_txt; }

static void a_init_tmp_paths(void) {
  const char * env_tmp = getenv("AROMA_TMP");
  if (env_tmp && env_tmp[0] == '/') {
    snprintf(g_tmp_dir, sizeof(g_tmp_dir), "%s", env_tmp);
  }
  else if (!a_volkey_mode() && access("/tmp", W_OK) == 0) {
    snprintf(g_tmp_dir, sizeof(g_tmp_dir), "/tmp/aroma");
  }
  else {
    /* Booted Android (BOOTMODE=1) often lacks writable /tmp; prefer /dev/tmp/aroma */
    if (access("/dev/tmp", W_OK) == 0 || mkdir("/dev/tmp", 0755) == 0) {
      snprintf(g_tmp_dir, sizeof(g_tmp_dir), "/dev/tmp/aroma");
    }
    else if (access("/tmp", W_OK) == 0) {
      snprintf(g_tmp_dir, sizeof(g_tmp_dir), "/tmp/aroma");
    }
    else {
      snprintf(g_tmp_dir, sizeof(g_tmp_dir), "/data/local/tmp/aroma");
    }
  }
  snprintf(g_tmp_s_dir, sizeof(g_tmp_s_dir), "%s-data", g_tmp_dir);
  snprintf(g_install_log, sizeof(g_install_log), "%s/.install.log", g_tmp_dir);
  snprintf(g_install_txt, sizeof(g_install_txt), "%s/.install.txt", g_tmp_dir);
}

static int a_read_proc_info(int pid, char * comm_buf, size_t comm_sz) {
  char path[64];
  char statbuf[512];
  FILE * fp;
  int ppid = 0;

  if (comm_buf && comm_sz > 0) {
    comm_buf[0] = '\0';
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    fp = fopen(path, "r");
    if (fp) {
      if (fgets(comm_buf, (int) comm_sz, fp)) {
        size_t len = strlen(comm_buf);
        while (len > 0 && (comm_buf[len - 1] == '\n' || comm_buf[len - 1] == '\r')) {
          comm_buf[--len] = '\0';
        }
      }
      fclose(fp);
    }
  }

  snprintf(path, sizeof(path), "/proc/%d/stat", pid);
  fp = fopen(path, "r");
  if (fp) {
    if (fgets(statbuf, sizeof(statbuf), fp)) {
      char * rparen = strrchr(statbuf, ')');
      if (rparen && rparen[1] == ' ' && rparen[2] != '\0') {
        sscanf(rparen + 2, "%*c %d", &ppid);
      }
    }
    fclose(fp);
  }
  return ppid;
}

static void a_pause_ancestors(void) {
  int chain[8];
  char comms[8][64];
  int count = 0;
  int rec_idx = -1;
  int pid = parent_pid;
  int i;

  paused_count = 0;
  while (pid > 1 && count < 8) {
    int next_ppid = a_read_proc_info(pid, comms[count], sizeof(comms[count]));
    if (strcmp(comms[count], "init") == 0 || strcmp(comms[count], "adbd") == 0) {
      break;
    }
    chain[count] = pid;
    if (strcmp(comms[count], "recovery") == 0 || strcmp(comms[count], "twrp") == 0) {
      rec_idx = count;
      count++;
      break;
    }
    count++;
    if (next_ppid <= 1 || next_ppid == pid) {
      break;
    }
    pid = next_ppid;
  }

  int stop_limit = (rec_idx >= 0) ? (rec_idx + 1) : (count > 0 ? 1 : 0);
  if (stop_limit > 0) {
    aroma_memory_parentpid(chain[stop_limit - 1]);
  }
  for (i = 0; i < stop_limit; i++) {
    LOGS("Mute Ancestor PID %d (%s)\n", chain[i], comms[i]);
    if (kill(chain[i], 19) == 0) {
      paused_pids[paused_count++] = chain[i];
    }
  }
}

static void a_resume_ancestors(void) {
  int i;
  for (i = paused_count - 1; i >= 0; i--) {
    LOGS("Unmute Ancestor PID %d\n", paused_pids[i]);
    kill(paused_pids[i], 18);
  }
  paused_count = 0;
}

/* Async-signal-safe crash guard: always SIGCONT paused recovery ancestors so Recovery never freezes */
static void a_crash_signal_handler(int sig) {
  int i;
  for (i = paused_count - 1; i >= 0; i--) {
    if (paused_pids[i] > 1) {
      kill(paused_pids[i], SIGCONT);
    }
  }
  paused_count = 0;
  _exit(128 + sig);
}

static void a_install_crash_guards(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = a_crash_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESETHAND;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS,  &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  sigaction(SIGFPE,  &sa, NULL);
  sigaction(SIGILL,  &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}

//*
//* Pass Recovery PIPE
//*
FILE * apipe() {
  return acmd_pipe;
}

//*
//* Init Reboot Request
//*
void a_check_reboot() {
  if (reboot_opt != 0) {
    fprintf(apipe(), "ui_print\n");
    
    if (strcmp(reboot_type, "") == 0) {
      fprintf(apipe(), "ui_print Rebooting...\n");
    }
    else {
      fprintf(apipe(), "ui_print Rebooting (%s)...\n", reboot_type);
    }
    
    fprintf(apipe(), "ui_print\n");
    usleep(2000000);
    
    if (strcmp(reboot_type, "") == 0) {
      reboot(RB_AUTOBOOT);
    }
    else {
      // Other
      syscall(__NR_reboot, 0xfee1dead, 672274793, 0xA1B2C3D4, reboot_type);
    }
  }
}

//*
//* Set Reboot Request
//*
void a_reboot(byte rtype, char * type) {
  reboot_opt = rtype;
  snprintf(reboot_type, 32, "%s", type);
}

//*
//* Get Command Argument
//*
char * getArgv(int id) {
  return currArgv[id];
}

//*
//* Show Text Splash
//*
void a_splash(char * spipe) {
  if (strcmp(spipe, "stdout") == 0 || strcmp(spipe, "-") == 0) {
    acmd_pipe = stdout;
    a_set_plain_output(1);
  }
  else {
    int fd    = atoi(spipe);
    acmd_pipe = (fd == 1) ? stdout : fdopen(fd, "wb");
    if (!acmd_pipe) {
      acmd_pipe = stdout;
    }
    if (fd == 1) {
      a_set_plain_output(1);
    }
  }
  setlinebuf(acmd_pipe);
  if (a_volkey_mode()) {
    avk_printf("Starting %s version %s (Volume Key Mode)", AROMA_NAME, AROMA_VERSION);
    avk_printf("  %s", AROMA_COPY);
    return;
  }
  //#-- Print Info Into Recovery
  fprintf(apipe(), "ui_print\n"
          "ui_print Starting " AROMA_NAME " version " AROMA_VERSION "\n"
          "ui_print\n"
          "ui_print " AROMA_COPY "\n"
          "ui_print\n"
          "ui_print\n"
         );
  usleep(600000);
}

//*
//* Init All Resources
//*
byte a_init_all() {
  //-- Init
  if (!ag_init()) {
    return 0;
  }
  ui_init();                        //-- Init Event Handler
  return 1;
}

//*
//* Release All Resources
//*
void a_release_all() {
  //-- Release All
  ag_closefonts();  //-- Release Fonts
  LOGS("Font Released\n");
  ev_exit();        //-- Release Input Engine
  LOGS("Input Released\n");
  az_close();       //-- Release Zip Handler
  LOGS("Archive Released\n");
  ag_close();       //-- Release Graph Engine
  LOGS("Graph Released\n");
}


//*
//* AROMA Installer Main Executable
//*
int main(int argc, char ** argv) {
#ifndef _AROMA_NODEBUG
  remove_directory("/tmp/aroma-memory");
  aroma_memory_debug_init();
#endif
  int retval = 1;
  parent_pid = getppid();
  //-- Normal Updater Sequences
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  //-- Parse optional flags (--volkey, --config-only, --plain) and env vars
  if (getenv("AROMA_VOLKEY") && strcmp(getenv("AROMA_VOLKEY"), "1") == 0) {
    a_set_volkey_mode(1);
  }
  if (getenv("AROMA_CONFIG_ONLY") && strcmp(getenv("AROMA_CONFIG_ONLY"), "1") == 0) {
    a_set_config_only_mode(1);
  }
  if (getenv("AROMA_PLAIN") && strcmp(getenv("AROMA_PLAIN"), "1") == 0) {
    a_set_plain_output(1);
  }

  int argi = 1;
  while (argi < argc && argv[argi][0] == '-' && argv[argi][1] == '-') {
    if (strcmp(argv[argi], "--volkey") == 0) {
      a_set_volkey_mode(1);
    }
    else if (strcmp(argv[argi], "--config-only") == 0) {
      a_set_config_only_mode(1);
      a_set_volkey_mode(1);
    }
    else if (strcmp(argv[argi], "--plain") == 0) {
      a_set_plain_output(1);
    }
    argi++;
  }

  int rem = argc - argi;
  //-- Check Arguments (<api> <outfd> <zipfile>)
  if (rem != 3) {
    LOGE("Unexpected Number of Arguments (%d)\n", rem);
    return 1;
  }

  char * arg_api = argv[argi];
  char * arg_fd  = argv[argi + 1];
  char * arg_zip = argv[argi + 2];

  //-- Resolve & Init Temporary Dir (Recovery: /tmp/aroma, Android BOOTMODE=1: /dev/tmp/aroma or $AROMA_TMP)
  a_init_tmp_paths();
  remove_directory(AROMA_TMP);
  unlink(AROMA_TMP_S);
  create_directory(AROMA_TMP);
  symlink(AROMA_TMP, AROMA_TMP_S);
  setenv("AROMA_TMP", AROMA_TMP, 1);
  setenv("AROMA_VOLKEY", a_volkey_mode() ? "1" : "0", 1);

  if (!a_volkey_mode()) {
    printf("Starting " AROMA_NAME " version " AROMA_VERSION "\n"
           "     " AROMA_COPY "\n");
  }

  //-- Check CWM Version
  if ((arg_api[0] != '1' && arg_api[0] != '2' && arg_api[0] != '3') || arg_api[1] != '\0') {
    LOGE("Wrong Updater Binary API!!! Expected 1, 2, or 3, But got %s\n", arg_api);
    return 2;
  }

  //-- Init Pipe & Show Splash Info
  a_splash(arg_fd);

  //-- Save Arguments
  if (!a_volkey_mode()) {
    LOGS("Saving Arguments\n");
  }
  snprintf(currArgv[0], 255, "%s", arg_api);
  snprintf(currArgv[1], 255, "%s", arg_zip);

  //-- Native Volume Key Selector Mode (BOOTMODE=1 / Magisk / KernelSU / APatch / Headless)
  if (a_volkey_mode()) {
    if (az_init(arg_zip)) {
      avk_init_input();
      if (aui_start()) {
        retval = 0;
      }
      avk_close_input();
      az_close();
    }
    else {
      avk_printf("! Cannot open archive: %s", arg_zip);
    }

    /* Keep AROMA_TMP intact in --config-only mode so customize.sh can read *.prop */
    if (!a_config_only_mode()) {
      unlink(AROMA_TMP_S);
      remove_directory(AROMA_TMP);
      a_check_reboot();
    }
    if (acmd_pipe && acmd_pipe != stdout) {
      fclose(acmd_pipe);
    }
    return retval;
  }

  //-- Mute Parent & Recovery Compositor (Touch GUI mode only)
  a_install_crash_guards();
  if (parent_pid) {
    a_pause_ancestors();
  }

  //-- Init Zip
  LOGS("Open Archive\n");
  
  if (az_init(arg_zip)) {
    //-- Initializing All Resources
    LOGS("Initializing Resource\n");
    if (!a_init_all()) {
      LOGE("No supported display backend (fbdev/DRM) available; restoring parent recovery.\n");
      az_close();
      retval = AROMA_EXIT_NO_DISPLAY;
    }
    else {
      //-- Starting AROMA Installer UI
      LOGS("Starting Interface\n");
      
      if (aui_start()) {
        fprintf(apipe(), "ui_print " AROMA_NAME " Finished...\nui_print\nui_print\n");
        retval = 0;
      }
      
      //-- Close Graph Thread
      LOGS("Close Graph Thread\n");
      ag_close_thread();
      //-- Wait Until Clean Up
      usleep(200000);
      //-- Release All Resource
      LOGS("Starting Release\n");
      a_release_all();
    }
  }
  else {
    LOGE("Cannot Open Archive\n");
  }
  
  //-- Unmute Recovery & Parent
  a_resume_ancestors();
  
  //-- REMOVE AROMA TEMPORARY
  LOGS("Cleanup Temporary\n");
  unlink(AROMA_TMP_S);
  remove_directory(AROMA_TMP);
  //-- Check Reboot Request
  LOGS("Check For Reboot\n");
  a_check_reboot();
  //-- Cleanup PIPE
  LOGS("Closing Recovery Pipe\n");
  if (acmd_pipe && acmd_pipe != stdout) {
    fclose(acmd_pipe);
  }
#ifndef _AROMA_NODEBUG
  aroma_dump_malloc();
#endif
  //-- Return Exit Status
  return retval;
}
