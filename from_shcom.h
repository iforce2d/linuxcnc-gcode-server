
#ifndef FROM_SHCOM
#define FROM_SHCOM

// The contents of this file are mostly taken from src/emc/usr_intf/shcom.hh in the LinuxCNC source,
// and reduced so that only the relevant parts are included without bringing in too many other dependencies.

#include "linuxcnc/emc.hh"
#include "linuxcnc/emc_nml.hh"
#include "linuxcnc/timer.hh"
#include "nml_oi.hh"

#define JOGTELEOP 0
#define JOGJOINT  1

enum EMC_UPDATE_TYPE {
    EMC_UPDATE_NONE = 1,
    EMC_UPDATE_AUTO
};

enum EMC_WAIT_TYPE {
    EMC_WAIT_RECEIVED = 2,
    EMC_WAIT_DONE
};

extern RCS_CMD_CHANNEL *emcCommandBuffer;
extern RCS_STAT_CHANNEL *emcStatusBuffer;
extern NML *emcErrorBuffer;

extern EMC_WAIT_TYPE emcWaitType;
extern EMC_UPDATE_TYPE emcUpdateType;

extern int emcCommandSerialNumber;

extern char error_string[NML_ERROR_LEN];
extern char operator_text_string[NML_TEXT_LEN];
extern char operator_display_string[NML_DISPLAY_LEN];

int emcTaskNmlGet();
int emcErrorNmlGet();
int tryNml(double retry_time, double retry_interval);
int updateStatus();
int updateError();
int emcCommandPollDone();
int emcCommandWaitDone();
int emcCommandWaitReceived();
int emcCommandSend(RCS_CMD_MSG & cmd);
int sendAbort();
int sendEstop();
int sendEstopReset();
int sendMachineOn();
int sendMachineOff();
int sendManual();
int sendAuto();
int sendMdi();
int sendSetTeleopEnable(int enable);
int sendHome(int joint);
int sendMdiCmd(const char *mdi);
int sendProgramOpen(const char *program);
int sendProgramRun(int line);
int sendProgramPause();
int sendProgramResume();
int sendJogStop(int ja, int jjogmode);
int sendJogCont(int ja, int jjogmode, double speed);
int sendJogIncr(int ja, int jjogmode, double speed, double incr);
int sendTrajSetAcceleration(double accel);
int sendTrajSetMaxAcceleration(double accel);
int sendSetDout(unsigned char index, unsigned char value);

#endif
