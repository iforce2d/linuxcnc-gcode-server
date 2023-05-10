
// Loosely based on src/emc/usr_intf/emcrsh.cc in the LinuxCNC source.

#define _REENTRANT

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <limits.h>
#include <getopt.h>
#include <iostream>
#include <cstdlib>

#include "linuxcnc/emc.hh"
#include "linuxcnc/emc_nml.hh"

#include "from_shcom.h"

#define VERSION_LEN   8
#define HOSTNAME_LEN   80

#define INBUF_LEN   256
#define OUTBUF_LEN  4096

typedef struct {  
  int cliSock;
  char hostName[HOSTNAME_LEN];
  char version[VERSION_LEN];
  bool linked;
  bool echo;
  bool verbose;
  bool enabled;
  int commMode;
  int commProt;
  char inBuf[INBUF_LEN];
  char outBuf[OUTBUF_LEN];
  char progName[PATH_MAX];
} connectionRecType;

int port = 5007;
int enableMachineOnStartup = 0;
int server_sockfd;
socklen_t server_len, client_len;
struct sockaddr_in server_address;
struct sockaddr_in client_address;
bool useSockets = true;
int tokenIdx;
const char *delims = " \n\r\0";
int enabledConn = -1;
char pwd[16] = "EMC\0";
char enablePWD[16] = "EMCTOO\0";
char serverName[24] = "EMCNETSVR\0";
int sessions = 0;
int maxSessions = -1;

struct option longopts[] = {
  {"help", 0, NULL, 'h'},
  {"port", 1, NULL, 'p'},
  {"enable", 0, NULL, 'e'},
//  {"name", 1, NULL, 'n'},
//  {"sessions", 1, NULL, 's'},
//  {"connectpw", 1, NULL, 'w'},
//  {"enablepw", 1, NULL, 'e'},
//  {"path", 1, NULL, 'd'},
  {0,0,0,0}};

static int initSockets()
{
  int optval = 1;
  int err;
  
  server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(port);
  server_len = sizeof(server_address);
  err = bind(server_sockfd, (struct sockaddr *)&server_address, server_len);
  if (err) {
      printf("error initializing sockets: %s\n", strerror(errno));
      return err;
  }

  err = listen(server_sockfd, 5);
  if (err) {
      printf("error listening on socket: %s\n", strerror(errno));
      return err;
  }
  
  // ignore SIGCHLD
  {
    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGCHLD, &act, NULL);
  }

  return 0;
}

static void sigQuit(int sig)
{
    printf("*** Received sigQuit\n");
    //thisQuit();
}

const char* getString_RCS_STATUS(RCS_STATUS s) {
    switch (s) {
        case UNINITIALIZED_STATUS:  return "UNINITIALIZED_STATUS";
        case RCS_DONE:              return "RCS_DONE";
        case RCS_EXEC:              return "RCS_EXEC";
        case RCS_ERROR:             return "RCS_ERROR";
    }
    return "?";
}

const char* getString_EMC_TASK_STATE( EMC_TASK_STATE_ENUM m) {
    switch (m) {
        case EMC_TASK_STATE_ESTOP:          return "EMC_TASK_STATE_ESTOP";
        case EMC_TASK_STATE_ESTOP_RESET:    return "EMC_TASK_STATE_ESTOP_RESET";
        case EMC_TASK_STATE_OFF:            return "EMC_TASK_STATE_OFF";
        case EMC_TASK_STATE_ON:             return "EMC_TASK_STATE_ON";
    }
    return "?";
}

const char* getString_EMC_TASK_MODE( EMC_TASK_MODE_ENUM m) {
    switch (m) {
        case EMC_TASK_MODE_MANUAL:  return "EMC_TASK_MODE_MANUAL";
        case EMC_TASK_MODE_AUTO:    return "EMC_TASK_MODE_AUTO";
        case EMC_TASK_MODE_MDI:     return "EMC_TASK_MODE_MDI";
    }
    return "?";
}

void showStatus() {
    int ok = updateStatus() == 0;
    if ( ok ) {
        printf("------\nstatus: %s\ntask.status: %s\ntask.mode: %s\nemcCommandSerialNumber: %d\necho_serial_number: %d\nhomed: %d %d %d %d\nPosition: %f %f %f %f\n",
               getString_RCS_STATUS( (RCS_STATUS)emcStatus->state ),
               getString_EMC_TASK_STATE( emcStatus->task.state ),
               getString_EMC_TASK_MODE( emcStatus->task.mode ),
               emcCommandSerialNumber,
               emcStatus->echo_serial_number,
               emcStatus->motion.joint[0].homed,
               emcStatus->motion.joint[1].homed,
               emcStatus->motion.joint[2].homed,
               emcStatus->motion.joint[3].homed,
               emcStatus->motion.traj.position.tran.x,
               emcStatus->motion.traj.position.tran.y,
               emcStatus->motion.traj.position.tran.z,
               emcStatus->motion.traj.position.a
        );
    }
    else
        printf("updateStatus failed\n");
    fflush(stdout);
}

bool allHomed() {
    updateStatus();
    return emcStatus->motion.joint[0].homed &&
           emcStatus->motion.joint[1].homed &&
           emcStatus->motion.joint[2].homed;
}

bool ensureHomed() {
    updateStatus();
    bool needHoming = false;
    for (int i = 0; i < 4; i++) {
        if ( ! emcStatus->motion.joint[i].homed ) {
            int ok = sendHome(i) == 0;
            printf("sendHome(%d) %s\n", i, ok ? "ok":"ng");
            needHoming = true;
        }
    }

    if ( !needHoming ) {
        printf("Already homed.\n");
        return true;
    }

    printf("Waiting for home completion...\n");
    for (int i = 0; !allHomed() && i < 1000; i++) {
        esleep(0.1);
    }

    showStatus();

    if ( ! allHomed() ) {
        printf("Homing failed!\n");
        return false;
    }

    return true;
}

void showError(connectionRecType *context = NULL) {
    updateError();
    printf("%s\n", error_string);
    if ( context ) {
        char buf[512];
        snprintf(buf, 512, "error: %s\n", error_string);
        write(context->cliSock, buf, strlen(buf));
    }
}

void strtoupper(char * t, int n) {
  char *s = t;
  int i = 0;
  while (*s && i < n) {
    *s = toupper((unsigned char) *s);
    s++;
    i++;
  }
}

typedef enum {
    cmdStatus = 0,
    cmdAbort,
    cmdOpenProgram,
    cmdRunProgram,
    cmdPause,
    cmdResume,
    cmdPosition,
    cmdFirmwareInfo,
    cmdGetAPinState,
    cmdFinishMoves,
    //cmdSetDPinState,
    //cmdSetAcceleration,
    cmdUnknown
} commandTokenType;

const char *commands[] = {"STATUS", "ABORT", "OPEN", "RUN", "PAUSE", "RESUME", "M114", "M115", "M105", "M400", /*"M42", "M171",*/ ""};

int lookupToken(char *s)
{
    int i = 0;
    while (i < cmdUnknown) {
        if (strcmp(commands[i], s) == 0)
            return i;
        i++;
    }
    return i;
}

void replyStatus( connectionRecType* context ) {

    updateStatus();

    showStatus(); // on server output as well

    char s[256];
    sprintf(s, "%d %d (%d %d %d) %f %f %f\n",
           emcStatus->task.state,
           emcStatus->task.mode,
           emcStatus->motion.joint[0].homed,
           emcStatus->motion.joint[1].homed,
           emcStatus->motion.joint[2].homed,
           emcStatus->motion.traj.position.tran.x,
           emcStatus->motion.traj.position.tran.y,
           emcStatus->motion.traj.position.tran.z
    );
    write(context->cliSock, s, strlen(s));
}

void replyPosition( connectionRecType* context ) {

    printf("Doing replyPosition\n"); fflush(stdout);

    updateStatus();

    char s[256];
    sprintf(s, "ok X:%f Y:%f Z:%f A:%f\n",
           emcStatus->motion.traj.position.tran.x,
           emcStatus->motion.traj.position.tran.y,
           emcStatus->motion.traj.position.tran.z,
           emcStatus->motion.traj.position.a
    );
    write(context->cliSock, s, strlen(s));
}

void replyFirmwareInfo( connectionRecType* context ) {

    printf("Doing replyFirmwareInfo\n"); fflush(stdout);

    const char* firmware = "ok FIRMWARE_NAME:linuxcnc-gcode, FIRMWARE_VERSION:0.1\n";
    write(context->cliSock, firmware, strlen(firmware));
}

void replyOk( connectionRecType* context ) {

    const char* okStr = "ok\n";
    write(context->cliSock, okStr, strlen(okStr));
}

void replyFinishMoves( connectionRecType* context ) {

    printf( "Doing finish moves... " ); fflush(stdout);

    int doneStatus = emcCommandPollDone();
    int count = 0;
    while ( ! doneStatus ) {
        count++;
        if ( count % 10000 == 0 ) {
            printf("waiting... "); fflush(stdout);
        }
        esleep(0.001);
        doneStatus = emcCommandPollDone();
    }

    if ( doneStatus < 1 ) {
        printf( "*** emcCommandPollDone failed!\n" );
        //showStatus();
    }
    else
        printf( "ok\n" );

    replyOk(context);
}

void doMDI(connectionRecType* context, char* inStr, bool wait = false) {

    printf( "Doing MDI command: %s\n", inStr );

    int ok = 0;

    // ensure MDI mode
    updateStatus();
    if ( emcStatus->task.mode != EMC_TASK_MODE_MDI ) {
        printf("sendMdi...");
        ok = sendMdi() == 0;
        printf("%s\n", ok ? "ok":"ng");
        if ( !ok ) {
            showError();
            return;
        }
    }

    updateStatus();
    if ( emcStatus->task.mode != EMC_TASK_MODE_MDI ) {
        const char* noMdi = "error: could not enter MDI mode\n";
        printf(noMdi);
        write(context->cliSock, noMdi, strlen(noMdi));
        return;
    }

    //emcWaitType = EMC_WAIT_DONE;

    printf("sendMdiCmd... "); fflush(stdout);
    ok = sendMdiCmd( inStr ) == 0;
    printf("%s\n", ok ? "ok":"ng");
    if ( !ok ) {
        showError(context);
    }
    else {
        if ( wait )
            replyFinishMoves(context);
        else
            replyOk(context);
    }
}

void getAPinState(connectionRecType* context) {

    printf( "Doing get analog pin state\n" );

    updateStatus();

    char s[256];
    sprintf(s, "ok T0:%f T1:%f T2:%f T3:%f\n",
            emcStatus->motion.analog_input[0],
            emcStatus->motion.analog_input[1],
            emcStatus->motion.analog_input[2],
            emcStatus->motion.analog_input[3]
    );
    write(context->cliSock, s, strlen(s));

}

void doAbort(connectionRecType* context) {

    printf("sendAbort...");
    int ok = sendAbort() == 0;
    printf("%s\n", ok ? "ok":"ng");
    if ( !ok )
        showError();
    else
        replyOk(context);
}

void doOpenProgram(connectionRecType* context, char* inStr) {

    printf( "Doing load file: %s\n", inStr );

    int ok = 0;

    printf("sendMdi...");
    ok = sendMdi() == 0;
    printf("%s\n", ok ? "ok":"ng");
    if ( !ok )
        showError();

    printf("sendProgramOpen... ");
    ok = sendProgramOpen("/home/pi/rsh/setupOcode.ngc") == 0;
    printf("%s\n", ok ? "ok":"ng");
    if ( ! ok )
        showError();
    else
        replyOk(context);
}

void doRunProgram(connectionRecType* context) {

    emcWaitType = EMC_WAIT_RECEIVED;

    int ok = 0;

    printf("sendAuto...");
    ok = sendAuto() == 0;
    printf("%s\n", ok ? "ok":"ng");
    if ( !ok )
        showError();

    ok = sendProgramRun(0) == 0;
    printf("sendProgramRun %s\n", ok ? "ok":"ng");
    if ( !ok )
        showError();
    else
        replyOk(context);
}

void doPause(connectionRecType* context) {

    int ok = 0;

    ok = sendProgramPause() == 0;
    printf("sendProgramPause %s\n", ok ? "ok":"ng");
    if ( !ok )
        showError();
    else
        replyOk(context);
}

void doResume(connectionRecType* context) {

    int ok = 0;

    ok = sendProgramResume() == 0;
    printf("sendProgramResume %s\n", ok ? "ok":"ng");
    if ( !ok )
        showError();
    else
        replyOk(context);
}

// handle the rsh command in context->inBuf
int parseCommand(connectionRecType *context)
{
    int ret = 0;

    printf("Parsing: %s\n", context->inBuf);

    strtoupper(context->inBuf, INBUF_LEN-1);

    char originalInBuf[INBUF_LEN];
    strncpy(originalInBuf, context->inBuf, INBUF_LEN-1);

    char* pch = strtok(context->inBuf, delims);
    if (pch != NULL) {

        switch (lookupToken(pch)) {
            case cmdStatus:
                replyStatus(context);
                break;
            case cmdPosition:
                replyPosition(context);
                break;
            case cmdFirmwareInfo:
                replyFirmwareInfo(context);
                break;
//            case cmdSetDPinState:
//                setDPinState(context, originalInBuf);
//                break;
            case cmdGetAPinState:
                getAPinState(context);
                break;
            case cmdFinishMoves:
                replyFinishMoves(context);
                break;
            case cmdAbort:
                doAbort(context);
                break;
            case cmdOpenProgram:
                doOpenProgram(context, originalInBuf);
                break;
            case cmdRunProgram:
                doRunProgram(context);
                break;
            case cmdPause:
                doPause(context);
                break;
            case cmdResume:
                doResume(context);
                break;
            default:
                doMDI(context, originalInBuf);
        }
    }

  return ret;
}  

void *readClient(void *arg)
{
  char buf[1600];
  int context_index;
  int i;
  int len;
  connectionRecType *context = (connectionRecType *)arg;

  context_index = 0;

  while (1) {
    // We always start this loop with an empty buf, though there may be one
    // partial line in context->inBuf[0..context_index].
    len = read(context->cliSock, buf, sizeof(buf));
    if (len < 0) {
      fprintf(stderr, "rsh: error reading from client: %s\n", strerror(errno));
      goto finished;
    }
    if (len == 0) {
      printf("rsh: eof from client\n");
      goto finished;
    }

    if (context->echo && context->linked)
      if(write(context->cliSock, buf, len) != (ssize_t)len) {
        fprintf(stderr, "rsh: write() failed: %s", strerror(errno));
      }

    for (i = 0; i < len; i ++) {
        if ((buf[i] != '\n') && (buf[i] != '\r')) {
            context->inBuf[context_index] = buf[i];
            context_index ++;
            continue;
        }

        // if we get here, i is the index of a line terminator in buf

        if (context_index > 0) {
            // we have some bytes in the context buffer, parse them now
            context->inBuf[context_index] = '\0';

            // The return value from parseCommand was meant to indicate
            // success or error, but it is unusable.  Some paths return
            // the return value of write(2) and some paths return small
            // positive integers (cmdResponseType) to indicate failure.
            // We're best off just ignoring it.
            (void)parseCommand(context);

            context_index = 0;
        }
    }
  }

finished:
  printf("rsh: disconnecting client %s (%s)\n", context->hostName, context->version);
  close(context->cliSock);
  free(context);
  pthread_exit((void *)0);
  sessions--;  // FIXME: not reached
}

int sockMain()
{
    int res;
    
    while (1) {
      int client_sockfd;

      client_len = sizeof(client_address);
      client_sockfd = accept(server_sockfd,
        (struct sockaddr *)&client_address, &client_len);
      if (client_sockfd < 0) exit(0);
      sessions++;
      if ((maxSessions == -1) || (sessions <= maxSessions)) {
        pthread_t *thrd;
        connectionRecType *context;

        thrd = (pthread_t *)calloc(1, sizeof(pthread_t));
        if (thrd == NULL) {
          fprintf(stderr, "rsh: out of memory\n");
          exit(1);
        }

        context = (connectionRecType *) malloc(sizeof(connectionRecType));
        if (context == NULL) {
          fprintf(stderr, "rsh: out of memory\n");
          exit(1);
        }

        context->cliSock = client_sockfd;
        context->linked = false;
        context->echo = true;
        context->verbose = false;
        strncpy(context->version, "1.0", VERSION_LEN-1);
        strncpy(context->hostName, "Default", HOSTNAME_LEN-1);
        context->enabled = false;
        context->commMode = 0;
        context->commProt = 0;
        context->inBuf[0] = 0;

        printf("Connection received\n");

        res = pthread_create(thrd, NULL, readClient, (void *)context);
      } else {
        res = -1;
      }
      if (res != 0) {
        close(client_sockfd);
        sessions--;
        }
     }
    return 0;
}

void initMain()
{
    emcWaitType = EMC_WAIT_DONE;
    emcCommandSerialNumber = 0;
    emcUpdateType = EMC_UPDATE_AUTO;
    emcCommandBuffer = 0;
    emcStatusBuffer = 0;
    emcStatus = 0;

    emcErrorBuffer = 0;
    error_string[LINELEN-1] = 0;
    operator_text_string[LINELEN-1] = 0;
    operator_display_string[LINELEN-1] = 0;
}

bool enableMachine() {

    emcWaitType = EMC_WAIT_RECEIVED;

    int ok = 0;

    printf("sendManual...");
    ok = sendManual() == 0;
    printf("%s\n", ok ? "ok":"ng");
    if ( !ok ) {
        showError();
        return false;
    }

    ok = sendSetTeleopEnable(0) == 0;
    printf("sendSetTeleopEnable %s\n", ok ? "ok":"ng");
    if ( !ok ) {
        showError();
        return false;
    }

    ok = sendEstopReset() == 0;
    printf("sendEstopReset %s\n", ok ? "ok":"ng");
    if ( !ok ) {
        showError();
        return false;
    }

    ok = sendMachineOn() == 0;
    printf("sendMachineOn %s\n", ok ? "ok":"ng");
    if ( !ok ) {
        showError();
        return false;
    }

    //showStatus();

    if ( ! ensureHomed() ) {
        printf("Could not home joints");
        return false;
    }

    printf("sendMdi...");
    ok = sendMdi() == 0;
    printf("%s\n", ok ? "ok":"ng");
    if ( !ok )
        showError();

    return ok;
}

void usage(char* pname) {
    printf("Usage: \n");
    printf("         %s [Options]\n"
           "Options:\n"
           "         --help     this help\n"
           "         --port     <port number>  (default=%d)\n"
           "         --enable   <0|1>  (default=%d) enable and home machine on startup\n"
          ,pname,port,enableMachineOnStartup
          );
}

int main(int argc, char *argv[])
{
    int opt;

    initMain();
    // process local command line args
    while((opt = getopt_long(argc, argv, "hen:p:s:w:d:", longopts, NULL)) != - 1) {
      switch(opt) {
        case 'h': usage(argv[0]); exit(1);
//        case 'e': strncpy(enablePWD, optarg, strlen(optarg) + 1); break;
//        case 'n': strncpy(serverName, optarg, strlen(optarg) + 1); break;
        case 'p': sscanf(optarg, "%d", &port); break;
          case 'e': enableMachineOnStartup = 1; break;
//        case 's': sscanf(optarg, "%d", &maxSessions); break;
//        case 'w': strncpy(pwd, optarg, strlen(optarg) + 1); break;
//        case 'd': strncpy(defaultPath, optarg, strlen(optarg) + 1);
        }
      }

    // process LinuxCNC command line args
    // Note: '--' may be used to separate cmd line args
    //       optind is index of next arg to process
    //       make argv[optind] zeroth arg
//    argc = argc - optind + 1;
//    argv = argv + optind - 1;
//    if (emcGetArgs(argc, argv) != 0) {
//        printf("error in argument list\n");
//        exit(1);
//    }

    // get configuration information
    //iniLoad(emc_inifile);
    if (initSockets()) {
        printf("error initializing sockets\n");
        exit(1);
    }
    // init NML
    if (tryNml(1, 1) != 0) {
        printf("can't connect to LinuxCNC\n");
        //thisQuit();
	exit(1);
    }

    // get current serial number, and save it for restoring when we quit
    // so as not to interfere with real operator interface
    updateStatus();
    emcCommandSerialNumber = emcStatus->echo_serial_number;

    if ( enableMachineOnStartup )
        enableMachine();
    showStatus();

    // attach our quit function to SIGINT
    {
        struct sigaction act;
        act.sa_handler = sigQuit;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        sigaction(SIGINT, &act, NULL);
    }

    // make all threads ignore SIGPIPE
    {
        struct sigaction act;
        act.sa_handler = SIG_IGN;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        sigaction(SIGPIPE, &act, NULL);
    }

    if (useSockets)
        sockMain();

    return 0;
}


