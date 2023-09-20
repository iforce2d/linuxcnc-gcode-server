
// The contents of this file are mostly taken from src/emc/usr_intf/shcom.cc in the LinuxCNC source,
// and reduced so that only the relevant parts are included without bringing in too many other dependencies.

#include "linuxcnc/emc.hh"
#include "linuxcnc/emc_nml.hh"
#include "linuxcnc/timer.hh"
#include "nml_oi.hh"
#include <unistd.h>
#include <iostream>
#include <cstdlib>
#include <cstring>

#include "inifile.hh"
#include "from_shcom.h"

static int num_joints = EMCMOT_MAX_JOINTS;

double emcTimeout;

EMC_UPDATE_TYPE emcUpdateType;
EMC_WAIT_TYPE emcWaitType = EMC_WAIT_DONE;

EMC_STAT *emcStatus;

RCS_CMD_CHANNEL *emcCommandBuffer;
RCS_STAT_CHANNEL *emcStatusBuffer;
NML *emcErrorBuffer;

char error_string[NML_ERROR_LEN];
char operator_text_string[NML_TEXT_LEN];
char operator_display_string[NML_DISPLAY_LEN];

int emcCommandSerialNumber;

int programStartLine = 0;

//const char *nmlfile = "/usr/share/linuxcnc/linuxcnc.nml";

char emc_macrosPath[LINELEN];
char emc_openFile[LINELEN];

int iniLoad(const char *filename)
{
    strncpy(emc_macrosPath, ".", LINELEN);
    strncpy(emc_openFile, "", LINELEN);

    IniFile inifile;
    const char *tmpstring;
    //char displayString[LINELEN] = "";
    //int t;
    //int i;

    // open it
    if (inifile.Open(filename) == false) {
        return -1;
    }

//    if (NULL != (inistring = inifile.Find("DEBUG", "EMC"))) {
//        // copy to global
//        if (1 != sscanf(inistring, "%i", &emc_debug)) {
//            emc_debug = 0;
//        }
//    } else {
//        // not found, use default
//        emc_debug = 0;
//    }

    if (NULL != (tmpstring = inifile.Find("NML_FILE", "EMC"))) {
        // copy to global
        strncpy(emc_nmlfile, tmpstring, LINELEN);
    } else {
        // not found, use default
    }

    if (NULL != (tmpstring = inifile.Find("SUBROUTINE_PATH", "RS274NGC"))) {
        // copy to global
        strncpy(emc_macrosPath, tmpstring, LINELEN);
    } else {
        // not found, use default
    }

    if (NULL != (tmpstring = inifile.Find("OPEN_FILE", "DISPLAY"))) {
        // copy to global
        strncpy(emc_openFile, tmpstring, LINELEN);
    } else {
        // not found, use default
    }


//    for (t = 0; t < EMCMOT_MAX_JOINTS; t++) {
//        jogPol[t] = 1;		// set to default
//        snprintf(displayString, sizeof(displayString), "JOINT_%d", t);
//        if (NULL != (inistring =
//                     inifile.Find("JOGGING_POLARITY", displayString)) &&
//            1 == sscanf(inistring, "%d", &i) && i == 0) {
//            // it read as 0, so override default
//            jogPol[t] = 0;
//        }
//    }

//    if (NULL != (inistring = inifile.Find("LINEAR_UNITS", "DISPLAY"))) {
//        if (!strcmp(inistring, "AUTO")) {
//            linearUnitConversion = LINEAR_UNITS_AUTO;
//        } else if (!strcmp(inistring, "INCH")) {
//            linearUnitConversion = LINEAR_UNITS_INCH;
//        } else if (!strcmp(inistring, "MM")) {
//            linearUnitConversion = LINEAR_UNITS_MM;
//        } else if (!strcmp(inistring, "CM")) {
//            linearUnitConversion = LINEAR_UNITS_CM;
//        }
//    } else {
//        // not found, leave default alone
//    }

//    if (NULL != (inistring = inifile.Find("ANGULAR_UNITS", "DISPLAY"))) {
//        if (!strcmp(inistring, "AUTO")) {
//            angularUnitConversion = ANGULAR_UNITS_AUTO;
//        } else if (!strcmp(inistring, "DEG")) {
//            angularUnitConversion = ANGULAR_UNITS_DEG;
//        } else if (!strcmp(inistring, "RAD")) {
//            angularUnitConversion = ANGULAR_UNITS_RAD;
//        } else if (!strcmp(inistring, "GRAD")) {
//            angularUnitConversion = ANGULAR_UNITS_GRAD;
//        }
//    } else {
//        // not found, leave default alone
//    }

    // close it
    inifile.Close();

    return 0;
}

int emcTaskNmlGet()
{
    int retval = 0;

    // try to connect to EMC cmd
    if (emcCommandBuffer == 0) {
        emcCommandBuffer =
            new RCS_CMD_CHANNEL(emcFormat, "emcCommand", "xemc", emc_nmlfile);
        if (!emcCommandBuffer->valid()) {
            delete emcCommandBuffer;
            emcCommandBuffer = 0;
            retval = -1;
        }
    }
    // try to connect to EMC status
    if (emcStatusBuffer == 0) {
        emcStatusBuffer =
            new RCS_STAT_CHANNEL(emcFormat, "emcStatus", "xemc", emc_nmlfile);
        if (!emcStatusBuffer->valid()
            || EMC_STAT_TYPE != emcStatusBuffer->peek()) {
            delete emcStatusBuffer;
            emcStatusBuffer = 0;
            emcStatus = 0;
            retval = -1;
        } else {
            emcStatus = (EMC_STAT *) emcStatusBuffer->get_address();
        }
    }

    return retval;
}

int emcErrorNmlGet()
{
    int retval = 0;

    if (emcErrorBuffer == 0) {
        emcErrorBuffer =
            new NML(nmlErrorFormat, "emcError", "xemc", emc_nmlfile);
        if (!emcErrorBuffer->valid()) {
            delete emcErrorBuffer;
            emcErrorBuffer = 0;
            retval = -1;
        }
    }

    return retval;
}

int tryNml(double retry_time, double retry_interval)
{
    double end;
    int good;

//    if ((emc_debug & EMC_DEBUG_NML) == 0) {
//        set_rcs_print_destination(RCS_PRINT_TO_NULL);	// inhibit diag
//        // messages
//    }
    end = retry_time;
    good = 0;
    do {
        if (0 == emcTaskNmlGet()) {
            good = 1;
            break;
        }
        esleep(retry_interval);
        end -= retry_interval;
    } while (end > 0.0);
//    if ((emc_debug & EMC_DEBUG_NML) == 0) {
//        set_rcs_print_destination(RCS_PRINT_TO_STDOUT);	// inhibit diag
//        // messages
//    }
    if (!good) {
        return -1;
    }

//    if ((emc_debug & EMC_DEBUG_NML) == 0) {
//        set_rcs_print_destination(RCS_PRINT_TO_NULL);	// inhibit diag
//        // messages
//    }
    end = retry_time;
    good = 0;
    do {
        if (0 == emcErrorNmlGet()) {
            good = 1;
            break;
        }
        esleep(retry_interval);
        end -= retry_interval;
    } while (end > 0.0);
//    if ((emc_debug & EMC_DEBUG_NML) == 0) {
//        set_rcs_print_destination(RCS_PRINT_TO_STDOUT);	// inhibit diag
//        // messages
//    }
    if (!good) {
        return -1;
    }

    return 0;
}

int updateStatus()
{
    NMLTYPE type;

    if (0 == emcStatus || 0 == emcStatusBuffer
        || !emcStatusBuffer->valid()) {
        return -1;
    }

    switch (type = emcStatusBuffer->peek()) {
    case -1:
        // error on CMS channel
        return -1;
        break;

    case 0:			// no new data
    case EMC_STAT_TYPE:	// new data
        // new data
        break;

    default:
        return -1;
        break;
    }

    return 0;
}

/*
  updateError() updates "errors," which are true errors and also
  operator display and text messages.
*/
int updateError()
{
    NMLTYPE type;

    if (0 == emcErrorBuffer || !emcErrorBuffer->valid()) {
        return -1;
    }

    type = emcErrorBuffer->read();

    //printf("------------ error type %d\n", type);

    switch (type) {
    case -1:
        // error reading channel
        return -1;
        break;

    case 0:
        // nothing new
        break;

    case EMC_OPERATOR_ERROR_TYPE:
        strncpy(error_string,
                ((EMC_OPERATOR_ERROR *) (emcErrorBuffer->get_address()))->
                error, LINELEN - 1);
        error_string[NML_ERROR_LEN - 1] = 0;
        break;

    case EMC_OPERATOR_TEXT_TYPE:
        strncpy(operator_text_string,
                ((EMC_OPERATOR_TEXT *) (emcErrorBuffer->get_address()))->
                text, LINELEN - 1);
        operator_text_string[NML_TEXT_LEN - 1] = 0;
        break;

    case EMC_OPERATOR_DISPLAY_TYPE:
        strncpy(operator_display_string,
                ((EMC_OPERATOR_DISPLAY *) (emcErrorBuffer->
                                           get_address()))->display,
                LINELEN - 1);
        operator_display_string[NML_DISPLAY_LEN - 1] = 0;
        break;

    case NML_ERROR_TYPE:
        strncpy(error_string,
                ((NML_ERROR *) (emcErrorBuffer->get_address()))->error,
                NML_ERROR_LEN - 1);
        error_string[NML_ERROR_LEN - 1] = 0;
        break;

    case NML_TEXT_TYPE:
        strncpy(operator_text_string,
                ((NML_TEXT *) (emcErrorBuffer->get_address()))->text,
                NML_TEXT_LEN - 1);
        operator_text_string[NML_TEXT_LEN - 1] = 0;
        break;

    case NML_DISPLAY_TYPE:
        strncpy(operator_display_string,
                ((NML_DISPLAY *) (emcErrorBuffer->get_address()))->display,
                NML_DISPLAY_LEN - 1);
        operator_display_string[NML_DISPLAY_LEN - 1] = 0;
        break;

    default:
        // if not recognized, set the error string
        snprintf(error_string, sizeof(error_string), "unrecognized error %d", type);
        return -1;
        break;
    }

    return type;
}

#define EMC_COMMAND_DELAY   0.01	// how long to sleep between checks

// returns
// 0 = not finished
// 1 = finished ok
// -1 = finished with error
int emcCommandPollDone()
{
    //printf("emcCommandWaitDone\n");

    updateStatus();

    int serial_diff = emcStatus->echo_serial_number - emcCommandSerialNumber;
    //printf("%d %d \n", emcCommandSerialNumber, emcStatus->echo_serial_number); fflush(stdout);
    if (serial_diff < 0) {
        //printf("0\n"); fflush(stdout);
        return 0;
    }

    if (serial_diff > 0) {
        return 1;
    }

    if (emcStatus->status == RCS_DONE) {
        return 1;
    }

    if (emcStatus->status == RCS_ERROR) {
        //printf("1\n"); fflush(stdout);
        return -1;
    }

    //printf("2\n"); fflush(stdout);
    return 0;
}

int emcCommandWaitDone()
{
    //printf("emcCommandWaitDone\n");

    double end;
    for (end = 0.0; emcTimeout <= 0.0 || end < emcTimeout; end += EMC_COMMAND_DELAY) {
        updateStatus();
        int serial_diff = emcStatus->echo_serial_number - emcCommandSerialNumber;
        if (serial_diff < 0) {
            continue;
        }

        if (serial_diff > 0) {
            return 0;
        }

        if (emcStatus->status == RCS_DONE) {
            return 0;
        }

        if (emcStatus->status == RCS_ERROR) {
            return -1;
        }

        esleep(EMC_COMMAND_DELAY);
    }

    return -1;
}

int emcCommandWaitReceived()
{
    //printf("emcCommandWaitReceived\n");
    double end;
    for (end = 0.0; emcTimeout <= 0.0 || end < emcTimeout; end += EMC_COMMAND_DELAY) {
        updateStatus();

        int serial_diff = emcStatus->echo_serial_number - emcCommandSerialNumber;
        if (serial_diff >= 0) {
            return 0;
        }

        esleep(EMC_COMMAND_DELAY);
    }

    return -1;
}

int emcCommandSend(RCS_CMD_MSG & cmd)
{
    // write command
    if (emcCommandBuffer->write(&cmd)) {
        return -1;
    }
    emcCommandSerialNumber = cmd.serial_number;
    return 0;
}

int sendAbort()
{
    EMC_TASK_ABORT task_abort_msg;

    emcCommandSend(task_abort_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendEstop()
{
    EMC_TASK_SET_STATE state_msg;

    state_msg.state = EMC_TASK_STATE_ESTOP;
    emcCommandSend(state_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendEstopReset()
{
    EMC_TASK_SET_STATE state_msg;

    state_msg.state = EMC_TASK_STATE_ESTOP_RESET;
    emcCommandSend(state_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendMachineOn()
{
    EMC_TASK_SET_STATE state_msg;

    state_msg.state = EMC_TASK_STATE_ON;
    emcCommandSend(state_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendMachineOff()
{
    EMC_TASK_SET_STATE state_msg;

    state_msg.state = EMC_TASK_STATE_OFF;
    emcCommandSend(state_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendManual()
{
    EMC_TASK_SET_MODE mode_msg;

    mode_msg.mode = EMC_TASK_MODE_MANUAL;
    emcCommandSend(mode_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendAuto()
{
    EMC_TASK_SET_MODE mode_msg;

    mode_msg.mode = EMC_TASK_MODE_AUTO;
    emcCommandSend(mode_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendMdi()
{
    EMC_TASK_SET_MODE mode_msg;

    mode_msg.mode = EMC_TASK_MODE_MDI;
    emcCommandSend(mode_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendSetTeleopEnable(int enable)
{
    EMC_TRAJ_SET_TELEOP_ENABLE emc_set_teleop_enable_msg;

    emc_set_teleop_enable_msg.enable = enable;
    emcCommandSend(emc_set_teleop_enable_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendHome(int joint)
{
    EMC_JOINT_HOME emc_joint_home_msg;

    emc_joint_home_msg.joint = joint;
    emcCommandSend(emc_joint_home_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendTrajSetAcceleration(double accel)
{
    EMC_TRAJ_SET_ACCELERATION emc_traj_set_accel;

    printf("accel = %f\n", accel);

    emc_traj_set_accel.acceleration = accel;
    emcCommandSend(emc_traj_set_accel);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendTrajSetMaxAcceleration(double accel)
{
    EMC_TRAJ_SET_MAX_ACCELERATION emc_traj_set_max_accel;

    printf("accel = %f\n", accel);

    emc_traj_set_max_accel.acceleration = accel;
    emcCommandSend(emc_traj_set_max_accel);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}



int sendMdiCmd(const char *mdi)
{
    EMC_TASK_PLAN_EXECUTE emc_task_plan_execute_msg;

    strncpy(emc_task_plan_execute_msg.command, mdi, LINELEN-1);
    emcCommandSend(emc_task_plan_execute_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

static char lastProgramFile[LINELEN] = "";

int sendProgramOpen(const char *program)
{
    EMC_TASK_PLAN_OPEN emc_task_plan_open_msg;

    // save this to run again
    strncpy(lastProgramFile, program, LINELEN-1);

    strncpy(emc_task_plan_open_msg.file, program, LINELEN-1);
    emcCommandSend(emc_task_plan_open_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendProgramRun(int line)
{
    EMC_TASK_PLAN_RUN emc_task_plan_run_msg;

    if (emcUpdateType == EMC_UPDATE_AUTO) {
        updateStatus();
    }
    // first reopen program if it's not open
    if (0 == emcStatus->task.file[0]) {
        // send a request to open last one
        sendProgramOpen(lastProgramFile);
    }
    // save the start line, to compare against active line later
    programStartLine = line;

    emc_task_plan_run_msg.line = line;
    emcCommandSend(emc_task_plan_run_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendProgramPause()
{
    EMC_TASK_PLAN_PAUSE emc_task_plan_pause_msg;

    emcCommandSend(emc_task_plan_pause_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendProgramResume()
{
    EMC_TASK_PLAN_RESUME emc_task_plan_resume_msg;

    emcCommandSend(emc_task_plan_resume_msg);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}

int sendJogStop(int ja, int jjogmode)
{
    EMC_JOG_STOP emc_jog_stop_msg;

    if (   (   (jjogmode == JOGJOINT)
            && (emcStatus->motion.traj.mode == EMC_TRAJ_MODE_TELEOP) )
        || (   (jjogmode == JOGTELEOP )
            && (emcStatus->motion.traj.mode != EMC_TRAJ_MODE_TELEOP) )
       ) {
       return -1;
    }

    if (  jjogmode &&  (ja < 0 || ja >= num_joints)) {
      fprintf(stderr,"shcom.cc: unexpected_1 %d\n",ja); return -1;
    }
    if ( !jjogmode &&  (ja < 0))                     {
      fprintf(stderr,"shcom.cc: unexpected_2 %d\n",ja); return -1;
    }

    emc_jog_stop_msg.jjogmode = jjogmode;
    emc_jog_stop_msg.joint_or_axis = ja;
    emcCommandSend(emc_jog_stop_msg);
    return 0;
}

int sendJogCont(int ja, int jjogmode, double speed)
{
    EMC_JOG_CONT emc_jog_cont_msg;

    if (emcStatus->task.state != EMC_TASK_STATE_ON) { return -1; }
    if (   (  (jjogmode == JOGJOINT)
            && (emcStatus->motion.traj.mode == EMC_TRAJ_MODE_TELEOP) )
        || (   (jjogmode == JOGTELEOP )
            && (emcStatus->motion.traj.mode != EMC_TRAJ_MODE_TELEOP) )
       ) {
       return -1;
    }

    if (  jjogmode &&  (ja < 0 || ja >= num_joints)) {
       fprintf(stderr,"shcom.cc: unexpected_3 %d\n",ja); return -1;
    }
    if ( !jjogmode &&  (ja < 0))                     {
       fprintf(stderr,"shcom.cc: unexpected_4 %d\n",ja); return -1;
    }

    emc_jog_cont_msg.jjogmode = jjogmode;
    emc_jog_cont_msg.joint_or_axis = ja;
    emc_jog_cont_msg.vel = speed / 60.0;

    emcCommandSend(emc_jog_cont_msg);

    return 0;
}

int sendJogIncr(int ja, int jjogmode, double speed, double incr)
{
    EMC_JOG_INCR emc_jog_incr_msg;

    if (emcStatus->task.state != EMC_TASK_STATE_ON) { return -1; }
    if (   ( (jjogmode == JOGJOINT)
        && (  emcStatus->motion.traj.mode == EMC_TRAJ_MODE_TELEOP) )
        || ( (jjogmode == JOGTELEOP )
        && (  emcStatus->motion.traj.mode != EMC_TRAJ_MODE_TELEOP) )
       ) {
       return -1;
    }

    if (  jjogmode &&  (ja < 0 || ja >= num_joints)) {
        fprintf(stderr,"shcom.cc: unexpected_5 %d\n",ja); return -1;
    }
    if ( !jjogmode &&  (ja < 0))                     {
        fprintf(stderr,"shcom.cc: unexpected_6 %d\n",ja); return -1;
    }

    emc_jog_incr_msg.jjogmode = jjogmode;
    emc_jog_incr_msg.joint_or_axis = ja;
    emc_jog_incr_msg.vel = speed / 60.0;
    emc_jog_incr_msg.incr = incr;

    emcCommandSend(emc_jog_incr_msg);

    return 0;
}

int sendSetDout(unsigned char index, unsigned char value)
{
    EMC_MOTION_SET_DOUT emc_motion_set_dout;

    emc_motion_set_dout.index = index;
    emc_motion_set_dout.start = value;
    emc_motion_set_dout.end = value;
    emc_motion_set_dout.now = 1;

    emcCommandSend(emc_motion_set_dout);
    if (emcWaitType == EMC_WAIT_RECEIVED) {
        return emcCommandWaitReceived();
    } else if (emcWaitType == EMC_WAIT_DONE) {
        return emcCommandWaitDone();
    }

    return 0;
}
