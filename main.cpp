
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
#include <errno.h>
#include <chrono>

#include <string>
#include <vector>

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
int autosendBatchTimeout = 250; // ms

//extern char emc_inifile[LINELEN];

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

int numJoints = 4;

std::string subRoutineFile = "tmp.ngc";

struct option longopts[] = {
  {"help", 0, NULL, 'h'},
  {"port", 1, NULL, 'p'},
  {"enable", 0, NULL, 'e'},
  {"inifile", 1, NULL, 'i'},
  {"timeout", 1, NULL, 't'},
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

const char* getString_EMC_TASK_STATE_short( EMC_TASK_STATE_ENUM m) {
    switch (m) {
        case EMC_TASK_STATE_ESTOP:          return "ESTOP";
        case EMC_TASK_STATE_ESTOP_RESET:    return "ESTOP_RESET";
        case EMC_TASK_STATE_OFF:            return "OFF";
        case EMC_TASK_STATE_ON:             return "ON";
    }
    return "?";
}

const char* getString_EMC_TASK_MODE_short( EMC_TASK_MODE_ENUM m) {
    switch (m) {
        case EMC_TASK_MODE_MANUAL:  return "MANUAL";
        case EMC_TASK_MODE_AUTO:    return "AUTO";
        case EMC_TASK_MODE_MDI:     return "MDI";
    }
    return "?";
}

void showStatus() {
    int ok = updateStatus() == 0;
    if ( ok ) {

        std::string homeFormat = "homed:";
        for (int i = 0; i < numJoints; i++) {
            homeFormat += " " + std::to_string(emcStatus->motion.joint[i].homed);
        }
        homeFormat += "\n";

        std::string statusFormat = "";
        statusFormat += "------\n";
        statusFormat += "status: %s\n";
        statusFormat += "task.status: %s\n";
        statusFormat += "task.mode: %s\n";
        statusFormat += homeFormat;
        statusFormat += "g5x offset: %f %f %f %f\n";
        statusFormat += "g92 offset: %f %f %f %f\n";
        statusFormat += "Commanded position: %f %f %f %f\n";
        statusFormat += "Actual position: %f %f %f %f\n";
        statusFormat += "Workspace pos: %f %f %f %f\n";
        statusFormat += "File: %s\n";

        float wsx = emcStatus->motion.traj.actualPosition.tran.x - emcStatus->task.g5x_offset.tran.x - emcStatus->task.g92_offset.tran.x;
        float wsy = emcStatus->motion.traj.actualPosition.tran.y - emcStatus->task.g5x_offset.tran.y - emcStatus->task.g92_offset.tran.y;
        float wsz = emcStatus->motion.traj.actualPosition.tran.z - emcStatus->task.g5x_offset.tran.z - emcStatus->task.g92_offset.tran.z;
        float wsa = emcStatus->motion.traj.actualPosition.a - emcStatus->task.g5x_offset.a - emcStatus->task.g92_offset.a;

        printf(statusFormat.c_str(),
                getString_RCS_STATUS( (RCS_STATUS)emcStatus->state ),
                getString_EMC_TASK_STATE( emcStatus->task.state ),
                getString_EMC_TASK_MODE( emcStatus->task.mode ),
//                emcStatus->motion.joint[0].homed,
//                emcStatus->motion.joint[1].homed,
//                emcStatus->motion.joint[2].homed,
//                emcStatus->motion.joint[3].homed,
                emcStatus->task.g5x_offset.tran.x,
                emcStatus->task.g5x_offset.tran.y,
                emcStatus->task.g5x_offset.tran.z,
                emcStatus->task.g5x_offset.a,
                emcStatus->task.g92_offset.tran.x,
                emcStatus->task.g92_offset.tran.y,
                emcStatus->task.g92_offset.tran.z,
                emcStatus->task.g92_offset.a,
                emcStatus->motion.traj.position.tran.x,
                emcStatus->motion.traj.position.tran.y,
                emcStatus->motion.traj.position.tran.z,
                emcStatus->motion.traj.position.a,
                emcStatus->motion.traj.actualPosition.tran.x,
                emcStatus->motion.traj.actualPosition.tran.y,
                emcStatus->motion.traj.actualPosition.tran.z,
                emcStatus->motion.traj.actualPosition.a,
                wsx,
                wsy,
                wsz,
                wsa,
                emcStatus->task.file
        );
    }
    else
        printf("updateStatus failed\n");
    fflush(stdout);
}

bool allHomed() {
    updateStatus();
    bool ok = true;
    for (int i = 0; i < numJoints; i++) {
        if ( ! emcStatus->motion.joint[i].homed ) {
            ok = false;
            break;
        }
    }
    return ok;
}

bool isHomed(int axisIndex) {
    updateStatus();
    return emcStatus->motion.joint[ axisIndex ].homed;
}

bool ensureHomeAll(int timeoutSeconds = 30) {

    updateStatus();
    bool needHoming = false;

    for (int i = 0; i < numJoints; i++) {
        if ( ! emcStatus->motion.joint[i].homed ) {
            needHoming = true;
        }
    }

    if ( !needHoming ) {
        printf("All joints already homed.\n");
        return true;
    }

    // send -1 to home all axes according to ini ordering
    int ok = sendHome(-1) == 0;
    printf("sendHome(%d) %s\n", -1, ok ? "ok":"ng");
    if ( updateError() > 0 )
        return false;

    printf("Waiting for home completion...\n");
    for (int i = 0; !allHomed() && i < timeoutSeconds * 10; i++) {
        esleep(0.1);
    }

    //showStatus();

    if ( ! allHomed() ) {
        printf("Homing failed!\n");
        return false;
    }

    printf("Homing done.\n");

    return true;
}

bool ensureHomeAxis(int axisIndex, int timeoutSeconds = 30) {

    updateStatus();
    bool needHoming = false;

    if ( ! emcStatus->motion.joint[axisIndex].homed ) {
        int ok = sendHome(axisIndex) == 0;
        printf("sendHome(%d) %s\n", axisIndex, ok ? "ok":"ng");
        if ( updateError() > 0 ) {
            return false;
        }
        needHoming = true;
    }

    if ( !needHoming ) {
        printf("Axis %d already homed.\n", axisIndex);
        return true;
    }

    printf("Waiting for home completion...\n");
    for (int i = 0; !isHomed(axisIndex) && i < timeoutSeconds * 10; i++) {
        esleep(0.1);
    }

    //showStatus();

    if ( ! isHomed(axisIndex) ) {
        printf("Homing failed!\n");
        return false;
    }

    printf("Homing done.\n");

    return true;
}

void showError(connectionRecType *context = NULL) {

    // some callers expect this function to update the string, but we shouldn't do it
    // when a string is already present, otherwise the current error will be skipped
    if ( error_string[0] == 0 )
        updateError();

    printf("%s\n", error_string);
    if ( context ) {
        char buf[512];
        snprintf(buf, 512, "error: %s\n", error_string);
        write(context->cliSock, buf, strlen(buf));
    }
    fflush(stdout);
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
    cmdEnable,
    cmdHome,
    cmdManual,
    cmdMdi,
    cmdOpenProgram,
    cmdShowFile,
    cmdRunProgram,
    cmdPause,
    cmdResume,
    cmdPosition,
    cmdFirmwareInfo,
    cmdGetAPinState,
    cmdFinishMoves,
    cmdBeginBatch,
    cmdEndBatch,
    //cmdSetDPinState,
    //cmdSetAcceleration,
    cmdUnknown
} commandTokenType;

const char *commands[] = {"STATUS", "ABORT", "ENABLE", "HOME", "MANUAL", "MDI", "OPEN", "FILE", "RUN", "PAUSE", "RESUME", "M114", "M115", "M105", "M400", "BEGINSUB", "ENDSUB", /*"M42", "M171",*/ ""};

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

    float wsx = emcStatus->motion.traj.actualPosition.tran.x - emcStatus->task.g5x_offset.tran.x - emcStatus->task.g92_offset.tran.x;
    float wsy = emcStatus->motion.traj.actualPosition.tran.y - emcStatus->task.g5x_offset.tran.y - emcStatus->task.g92_offset.tran.y;
    float wsz = emcStatus->motion.traj.actualPosition.tran.z - emcStatus->task.g5x_offset.tran.z - emcStatus->task.g92_offset.tran.z;
    float wsa = emcStatus->motion.traj.actualPosition.a - emcStatus->task.g5x_offset.a - emcStatus->task.g92_offset.a;

    std::string homeFormat = "homed:";
    for (int i = 0; i < numJoints; i++) {
        homeFormat += " " + std::to_string(emcStatus->motion.joint[i].homed);
    }

    char s[256];
    sprintf(s, "%s %s (%s) %f %f %f %f\n",
           getString_EMC_TASK_STATE_short( emcStatus->task.state ),
           getString_EMC_TASK_MODE_short( emcStatus->task.mode ),
           homeFormat.c_str(),
//           emcStatus->motion.joint[0].homed,
//           emcStatus->motion.joint[1].homed,
//           emcStatus->motion.joint[2].homed,
//           emcStatus->motion.joint[3].homed,
//           emcStatus->motion.traj.position.tran.x,
//           emcStatus->motion.traj.position.tran.y,
//           emcStatus->motion.traj.position.tran.z,
//           emcStatus->motion.traj.position.a
            wsx,
            wsy,
            wsz,
            wsa
    );
    write(context->cliSock, s, strlen(s));
}

void replyPosition( connectionRecType* context ) {

    printf("Doing replyPosition\n"); fflush(stdout);

    updateStatus();

    float wsx = emcStatus->motion.traj.actualPosition.tran.x - emcStatus->task.g5x_offset.tran.x - emcStatus->task.g92_offset.tran.x;
    float wsy = emcStatus->motion.traj.actualPosition.tran.y - emcStatus->task.g5x_offset.tran.y - emcStatus->task.g92_offset.tran.y;
    float wsz = emcStatus->motion.traj.actualPosition.tran.z - emcStatus->task.g5x_offset.tran.z - emcStatus->task.g92_offset.tran.z;
    float wsa = emcStatus->motion.traj.actualPosition.a - emcStatus->task.g5x_offset.a - emcStatus->task.g92_offset.a;

    char s[256];
    sprintf(s, "ok X:%f Y:%f Z:%f A:%f\n",
//           emcStatus->motion.traj.position.tran.x,
//           emcStatus->motion.traj.position.tran.y,
//           emcStatus->motion.traj.position.tran.z,
//           emcStatus->motion.traj.position.a
            wsx,
            wsy,
            wsz,
            wsa
    );
    write(context->cliSock, s, strlen(s));
}

void replyFirmwareInfo( connectionRecType* context ) {

    printf("Doing replyFirmwareInfo\n"); fflush(stdout);

    const char* firmware = "ok FIRMWARE_NAME:linuxcnc-gcode, FIRMWARE_VERSION:0.1\n";
    write(context->cliSock, firmware, strlen(firmware));
}

void replyOk( connectionRecType* context ) {

    if ( ! context )
        return;

    const char* okStr = "ok\n";
    write(context->cliSock, okStr, strlen(okStr));
}

void replyFinishMoves( connectionRecType* context ) {

    printf( "Doing finish moves... " ); fflush(stdout);

    int doneStatus = emcCommandPollDone();
    int count = 0;
    while ( ! doneStatus ) {
        count++;
        if ( count % 5000 == 0 ) {
            printf("still waiting... "); fflush(stdout);
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

bool ensureTaskMode(connectionRecType* context, EMC_TASK_MODE_ENUM whichMode) {

    if ( whichMode == EMC_TASK_MODE_AUTO )
        return false;

    const char* modeName = getString_EMC_TASK_MODE_short(whichMode);

    updateStatus();
    if ( emcStatus->task.mode != whichMode ) {
        printf("send mode change to %s ..." , modeName);
        int res = (whichMode == EMC_TASK_MODE_MDI) ? sendMdi() : sendManual();
        int ok = res == 0;
        printf("%s\n", ok ? "ok":"ng");
        if ( !ok ) {
            showError(context);
            return false;
        }
    }

    updateStatus();
    if ( emcStatus->task.mode != whichMode ) {
        char buf[128];
        sprintf(buf, "error: could not enter %s mode\n", modeName);
        printf(buf);
        if ( context )
            write(context->cliSock, buf, strlen(buf));
        return false;
    }

    return true;
}

// context can be NULL for this one if a batch is being auto-cleared
void doMDI(connectionRecType* context, const char* inStr, bool wait = false) {

    printf( "Doing MDI command: %s\n", inStr );

    if ( ! ensureTaskMode(context, EMC_TASK_MODE_MDI) )
        return;

    //emcWaitType = EMC_WAIT_DONE;

    printf("sendMdiCmd... "); fflush(stdout);
    int ok = sendMdiCmd( inStr ) == 0;
    printf("%s\n", ok ? "ok":"ng");

    usleep( 100 ); // seems to be necessary before the updateStatus below will detect any problems

    updateStatus();
    if ( updateError() > 0 )
        ok = false;

    if ( context ) {
        if ( ! ok ) {
            showError(context);
        }
        else {
            if ( wait )
                replyFinishMoves(context);
            else
                replyOk(context);
        }
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
        showError(context);
    else
        replyOk(context);
}

bool estopOffAndMachineOn(connectionRecType *context) {

    printf("sendEstopReset...");
    int ok = sendEstopReset() == 0;
    if ( !ok ) {
        showError(context);
        return false;
    }
    else {
        printf("ok\nsendMachineOn...");
        ok = sendMachineOn() == 0;
        if ( !ok ) {
            showError(context);
            return false;
        }
    }

    printf("ok\n");
    replyOk(context);

    return true;
}

bool setModeManual(connectionRecType *context){
    int ok = 0;

    printf("sendManual...");
    ok = sendManual() == 0;
    printf("%s\n", ok ? "ok":"ng");

    updateStatus();
    ok = ( emcStatus->task.mode == EMC_TASK_MODE_MANUAL );
    if ( ! ok )
        showError(context);
    else
        replyOk(context);

    return ok;
}

bool setModeMdi(connectionRecType *context){
    int ok = 0;

    printf("sendMdi...");
    ok = sendMdi() == 0;
    printf("%s\n", ok ? "ok":"ng");

    updateStatus();
    showStatus();
    ok = ( emcStatus->task.mode == EMC_TASK_MODE_MDI );
    if ( ! ok )
        showError(context);
    else
        replyOk(context);

    return ok;
}

bool doHome(connectionRecType *context, char* inStr){

    if ( ! ensureTaskMode(context, EMC_TASK_MODE_MANUAL) )
        return false;

    bool ok = false;

    char* axisStr = strtok(inStr, delims); // remove "home"
    if ( axisStr != NULL ) {
        axisStr = strtok(NULL, delims);
    }

    if ( ! axisStr ) {
        printf("doHome all ...\n");
        ok = ensureHomeAll();
    }
    else {
        int axisIndex = atoi( axisStr );
        if ( axisIndex < 0 ) {
            printf("doHome all ...\n");
            ok = ensureHomeAll();
        }
        else {
            printf("doHome %d ...\n", axisIndex);
            ok = ensureHomeAxis(axisIndex);
        }
    }

    if ( !ok ) {
        showError(context);
    }
    else
        replyOk(context);

    return ok;
}

void doShowFile(connectionRecType* context) {

    updateStatus();

    char buf[512];
    if ( emcStatus->task.file[0] )
        sprintf(buf, "%s\n", emcStatus->task.file);
    else
        sprintf(buf, "No file loaded\n");
    write(context->cliSock, buf, strlen(buf));

}

void doOpenProgram(connectionRecType* context, char* inStr) {

    printf( "Doing load file: %s\n", inStr );

    char* filenameStr = strtok(inStr, delims); // remove "open"
    if ( filenameStr != NULL ) {
        filenameStr = strtok(NULL, delims);
    }

    printf( "  Filename: %s\n", filenameStr );

    int ok = 0;

    printf("sendMdi...");
    ok = sendMdi() == 0;
    printf("%s\n", ok ? "ok":"ng");
    if ( !ok )
        showError(context);

    printf("sendProgramOpen... ");
    ok = sendProgramOpen( filenameStr ) == 0;
    printf("%s\n", ok ? "ok":"ng");

    updateStatus();
    if ( updateError() > 0 )
        ok = false;

    if ( ! ok )
        showError(context);
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
        showError(context);

    ok = sendProgramRun(0) == 0;
    printf("sendProgramRun %s\n", ok ? "ok":"ng");

    updateStatus();
    ok = ( emcStatus->task.mode == EMC_TASK_MODE_AUTO ) && (updateError() == 0);

    if ( !ok )
        showError(context);
    else
        replyOk(context);
}

void doPause(connectionRecType* context) {

    int ok = 0;

    ok = sendProgramPause() == 0;
    printf("sendProgramPause %s\n", ok ? "ok":"ng");
    if ( !ok )
        showError(context);
    else
        replyOk(context);
}

void doResume(connectionRecType* context) {

    int ok = 0;

    ok = sendProgramResume() == 0;
    printf("sendProgramResume %s\n", ok ? "ok":"ng");
    if ( !ok )
        showError(context);
    else
        replyOk(context);
}

bool insideBatch = false;
std::vector<std::string> batchEntries;
pthread_mutex_t batchEntriesLock;
std::chrono::steady_clock::time_point lastBatchCommandTime = {};

void beginBatch(connectionRecType* context) {

    printf( "Beginning batch\n" );

    pthread_mutex_lock(&batchEntriesLock);
    insideBatch = true;
    pthread_mutex_unlock(&batchEntriesLock);

    replyOk(context);

}

void queueBatchEntry(connectionRecType* context, char* inStr) {
    printf( "Adding to batch: %s\n", inStr );

    pthread_mutex_lock(&batchEntriesLock);
    batchEntries.push_back( std::string(inStr) );
    lastBatchCommandTime = std::chrono::steady_clock::now();
    pthread_mutex_unlock(&batchEntriesLock);

    replyOk(context);
}

void endBatch(connectionRecType* context) {

    pthread_mutex_lock(&batchEntriesLock);

    if ( batchEntries.empty() ) {
        insideBatch = false;
        pthread_mutex_unlock(&batchEntriesLock);
        replyOk(context);
        return;
    }

    printf( "Finalizing batch:\n" );
    std::vector<std::string>::iterator it = batchEntries.begin();
    while (it != batchEntries.end()) {
        std::string s = *it;
        printf( "   %s\n", s.c_str() );
        ++it;
    }

    if ( batchEntries.size() == 1 ) {

        printf( "Sending as single command\n" );

        std::string s = *batchEntries.begin();

        batchEntries.clear();
        insideBatch = false;
        pthread_mutex_unlock(&batchEntriesLock);

        doMDI(context, s.c_str());

        return;
    }

    printf( "Sending as subroutine\n" );
    FILE* f = fopen(subRoutineFile.c_str(), "w");
    if ( !f ) {
        printf( "*** Could not open file %s\n", subRoutineFile.c_str() );

        batchEntries.clear();
        insideBatch = false;
        pthread_mutex_unlock(&batchEntriesLock);

        sprintf(error_string, "batch send failed, could not open tmp file");
        showError(context);
        return;
    }

    fprintf(f, "o<tmp> sub\n");

    it = batchEntries.begin();
    while (it != batchEntries.end()) {
        std::string s = *it;
        fprintf(f, "%s\n", s.c_str());
        ++it;
    }

    fprintf(f, "o<tmp> endsub\n");
    fclose(f);

    batchEntries.clear();
    insideBatch = false;
    pthread_mutex_unlock(&batchEntriesLock);

    doMDI(context, "o<tmp> call");
    //replyOk(context);

}

void clearErrorBuffer() {

    updateStatus();
    while ( updateError() > 0 ) {
        //updateStatus();
    }
}

int parseCommand(connectionRecType *context)
{
    int ret = 0;

    clearErrorBuffer();

    printf("Parsing: %s\n", context->inBuf);

    char originalInBuf[INBUF_LEN];
    strncpy(originalInBuf, context->inBuf, INBUF_LEN-1);

    strtoupper(context->inBuf, INBUF_LEN-1);

    char* pch = strtok(context->inBuf, delims);

    if ( insideBatch ) {
        switch (lookupToken(pch)) {
            case cmdStatus:
            case cmdBeginBatch:
            case cmdFirmwareInfo:
            case cmdPosition:
            case cmdGetAPinState:
            case cmdFinishMoves:
            case cmdAbort:
            case cmdHome:
            case cmdManual:
            case cmdMdi:
            case cmdEnable:
            case cmdShowFile:
            case cmdRunProgram:
            case cmdPause:
            case cmdResume:
                // ignore
                printf("(ignored inside batch)\n");
                replyOk(context);
                break;
            case cmdEndBatch:
                endBatch(context);
                break;
            default:
                queueBatchEntry(context, originalInBuf);
        }
    }
    else {
        if (pch != NULL) {

            switch (lookupToken(pch)) {
                case cmdBeginBatch:
                    beginBatch(context);
                    break;
                case cmdEndBatch:
                    endBatch(context);
                    break;

                case cmdStatus:
                    replyStatus(context);
                    break;
                case cmdFirmwareInfo:
                    replyFirmwareInfo(context);
                    break;
                case cmdPosition:
                    replyPosition(context);
                    break;
                case cmdGetAPinState:
                    getAPinState(context);
                    break;
                case cmdFinishMoves:
                    replyFinishMoves(context);
                    break;
                case cmdAbort:
                    doAbort(context);
                    break;
                case cmdHome:
                    doHome(context, originalInBuf);
                    break;
                case cmdManual:
                    setModeManual(context);
                    break;
                case cmdMdi:
                    setModeMdi(context);
                    break;
                case cmdEnable:
                    estopOffAndMachineOn(context);
                    break;
                case cmdOpenProgram:
                    doOpenProgram(context, originalInBuf);
                    break;
                case cmdShowFile:
                    doShowFile(context);
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
      fprintf(stderr, "Error reading from client: %s\n", strerror(errno));
      goto finished;
    }
    if (len == 0) {
      printf("EOF from client\n");
      goto finished;
    }

    if (context->echo && context->linked)
      if(write(context->cliSock, buf, len) != (ssize_t)len) {
        fprintf(stderr, "write() failed: %s", strerror(errno));
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
  printf("Disconnecting client %s (%s)\n", context->hostName, context->version);
  close(context->cliSock);
  free(context);
  pthread_exit((void *)0);
  sessions--;  // FIXME: not reached
}

void *checkTimeout(void *arg)
{
    int ms = 250;
    int sleepTime = ms * 1000;

    //printf("Starting timeout thread\n");
    while (1) {
        usleep( sleepTime );
        //printf("Checking timeout...\n");

        std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
        std::chrono::milliseconds elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - lastBatchCommandTime);
        long long elapsed_ms_count = elapsed_ms.count();

        if (elapsed_ms_count > autosendBatchTimeout)
        {
            pthread_mutex_lock(&batchEntriesLock);
            if ( insideBatch && !batchEntries.empty() ) {
                printf("****** auto-clearing batch \n");
                pthread_mutex_unlock(&batchEntriesLock);
                endBatch(NULL);
            }
            else
                pthread_mutex_unlock(&batchEntriesLock);
        }
    }
    pthread_exit((void *)0);
}

int sockMain()
{
    int res;
    
    while (1) {
      int client_sockfd;

      client_len = sizeof(client_address);
      client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_address, &client_len);
      if (client_sockfd < 0)
          exit(0);
      sessions++;
      if ((maxSessions == -1) || (sessions <= maxSessions)) {
        pthread_t *thrd;
        connectionRecType *context;

        thrd = (pthread_t *)calloc(1, sizeof(pthread_t));
        if (thrd == NULL) {
          fprintf(stderr, "Out of memory\n");
          exit(1);
        }

        context = (connectionRecType *) malloc(sizeof(connectionRecType));
        if (context == NULL) {
          fprintf(stderr, "Out of memory\n");
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

        batchEntries.clear();
        insideBatch = false;

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

void startTimeoutThread() {

    pthread_t *thrd = (pthread_t *)calloc(1, sizeof(pthread_t));
    if (thrd == NULL) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    int res = pthread_create(thrd, NULL, checkTimeout, (void *)NULL);
    if ( res != 0 ) {
        printf("Could not start timeout thread\n");
    }
}

void initMain()
{
    emcWaitType = EMC_WAIT_RECEIVED;
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

    if ( ! ensureHomeAll() ) {
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
           "         --enable   enable and home machine on startup\n"
           "         --timeout  <milliseconds>  (default=%d) auto-send batch if no endsub given within this duration after last command\n"
           "         --inifile  <inifile>  (default=%s)\n"
          , pname, port, autosendBatchTimeout, emc_inifile
    );
}

int main(int argc, char *argv[])
{
    int opt;
printf("LINELEN= %d\n", LINELEN);
    initMain();
    // process local command line args
    while((opt = getopt_long(argc, argv, "hp:ei:t:", longopts, NULL)) != - 1) {
      switch(opt) {
        case 'h': usage(argv[0]); exit(1);
//        case 'e': strncpy(enablePWD, optarg, strlen(optarg) + 1); break;
//        case 'n': strncpy(serverName, optarg, strlen(optarg) + 1); break;
        case 'p': sscanf(optarg, "%d", &port); break;
          case 't': sscanf(optarg, "%d", &autosendBatchTimeout); break;
          case 'e': enableMachineOnStartup = 1; break;
//        case 's': sscanf(optarg, "%d", &maxSessions); break;
//        case 'w': strncpy(pwd, optarg, strlen(optarg) + 1); break;
//        case 'd': strncpy(defaultPath, optarg, strlen(optarg) + 1);
        case 'i': strncpy(emc_inifile, optarg, strlen(optarg) + 1);
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
    iniLoad(emc_inifile);

    printf("Using ini file        : %s\n", emc_inifile);
    printf("Using nml file        : %s\n", emc_nmlfile);
    printf("Using subroutine path : %s\n", emc_macrosPath);

    subRoutineFile = std::string(emc_macrosPath) + "/tmp.ngc";

    if (pthread_mutex_init(&batchEntriesLock, NULL) != 0) {
        printf("pthread_mutex_init failed\n");
        return 1;
    }

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

    numJoints = emcStatus->motion.traj.joints;
    printf("Number of joints      : %d\n", numJoints);

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

    startTimeoutThread();

    if (useSockets)
        sockMain();

    pthread_mutex_destroy(&batchEntriesLock);

    return 0;
}


