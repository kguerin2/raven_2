/**
* File: rt_raven.cpp
* Created 13-oct-2011 by Hawkeye
*
*   Runs all raven control functions.
*   Code split out from rt_process_preempt.cpp, in order to provide more flexibility.
*
*/

#include <ros/ros.h>

#include "rt_raven.h"
#include "defines.h"

#include "init.h"             // for initSurgicalArms()
#include "inv_kinematics.h"
#include "inv_cable_coupling.h"
#include "state_estimate.h"
#include "pid_control.h"
#include "grav_comp.h"
#include "t_to_DAC_val.h"
#include "fwd_cable_coupling.h"
#include "fwd_kinematics.h"
#include "trajectory.h"
#include "homing.h"
#include "local_io.h"
#include "update_device_state.h"

extern int NUM_MECH;
extern unsigned long int gTime;
extern struct DOF_type DOF_types[];
extern t_controlmode newRobotControlMode;

int raven_cartesian_space_command(struct device *device0, struct param_pass *currParams);
int raven_joint_velocity_control(struct device *device0, struct param_pass *currParams);
int raven_motor_position_control(struct device *device0, struct param_pass *currParams);
int raven_homing(struct device *device0, struct param_pass *currParams, int begin_homing=0);
int applyTorque(struct device *device0, struct param_pass *currParams);
int raven_sinusoidal_joint_motion(struct device *device0, struct param_pass *currParams);

extern int initialized;

/**
* controlRaven()
*   Implements control for one loop cycle.
*     precondition:  encoders values have been read,
*                    runlevel has been set
*     postcondition: robot state is reflected in device0,
*                    DAC outputs are set in device0
*
*/
int controlRaven(struct device *device0, struct param_pass *currParams){
    int ret = 0;
    t_controlmode controlmode = (t_controlmode)currParams->robotControlMode;

    //Initi zalization code
    initRobotData(device0, currParams->runlevel, currParams);

    //Compute Mpos & Velocities
    stateEstimate(device0);

    //Foward Cable Coupling
    fwdCableCoupling(device0, currParams->runlevel);

    //Forward kinematics
    fwdKin(device0, currParams->runlevel);

    switch (controlmode){
        case no_control:
            break;

        case cartesian_space_control:
            //initialized = false;
            ret = raven_cartesian_space_command(device0,currParams);
            break;

        case motor_pd_control:
            initialized = false;
            ret = raven_motor_position_control(device0,currParams);
            break;

        case joint_velocity_control:
            initialized = false;
            ret = raven_joint_velocity_control(device0, currParams);
            break;

        case homing_mode:
            initialized = false;
            //initialized = robot_ready(device0) ? true:false;
            ret = raven_homing(device0, currParams);
            set_posd_to_pos(device0);
            updateMasterRelativeOrigin(device0);
            if (robot_ready(device0))
            {
                currParams->robotControlMode = cartesian_space_control;
                newRobotControlMode = cartesian_space_control;
            }
            break;

        case apply_arbitrary_torque:
            initialized = false;
            ret = applyTorque(device0, currParams);
            break;

        case multi_dof_sinusoid:
            initialized = false;
            ret = raven_sinusoidal_joint_motion(device0, currParams);
            break;

        default:
            ROS_ERROR("Error: unknown control mode in controlRaven (rt_raven.cpp)");
            ret = -1;
            break;
    }

    return ret;
}

/**
* raven_cartesian_space_command()
*     runs pd_control on motor position.
*     Why is it called "end_effector_control?  Why's your mom so fat?
*/
int raven_cartesian_space_command(struct device *device0, struct param_pass *currParams){

    struct DOF *_joint = NULL;
    struct mechanism* _mech = NULL;
    int i=0,j=0;

    if (currParams->runlevel != RL_PEDAL_DN)
    {
        set_posd_to_pos(device0);
        updateMasterRelativeOrigin(device0);
    }

    // Set desired transform to straight down
    for (int i=0;i<NUM_MECH;i++)
    {
        _mech = &(device0->mech[i]);
        _mech->ori_d.R[0][0] = -1.0;
        _mech->ori_d.R[0][1] = 0.0;
        _mech->ori_d.R[0][2] = 0.0;

        _mech->ori_d.R[1][0] = 0.0;
        _mech->ori_d.R[1][1] = -1.0;
        _mech->ori_d.R[1][2] = 0.0;

        _mech->ori_d.R[2][0] = 0.0;
        _mech->ori_d.R[2][1] = 0.0;
        _mech->ori_d.R[2][2] = 1.0;
    }

    //Inverse kinematics
    invKin(device0, currParams);

    //Inverse Cable Coupling
    invCableCoupling(device0, currParams->runlevel);

    // Set all joints to zero torque
    _mech = NULL;  _joint = NULL;
    while (loop_over_joints(device0, _mech, _joint, i,j) )
    {
        if (currParams->runlevel != RL_PEDAL_DN)
        {
            _joint->tau_d=0;
        }
        else
        {
            mpos_PD_control(_joint);
        }
//        if (_joint->type == TOOL_ROT_GREEN)
//            log_msg("trg: jp:%0.4f\t jpd:%0.4f\t taud:%0.4f",
//                _joint->jpos, _joint->jpos_d, _joint->tau_d);

        TorqueToDAC(device0);
    }
    //    gravComp(device0);

    return 0;
}


int raven_joint_position_command(struct device *device0, struct param_pass *currParams){

    struct DOF *_joint = NULL;
    struct mechanism* _mech = NULL;
    int i=0,j=0;

    if (currParams->runlevel != RL_PEDAL_DN)
    {
        set_posd_to_pos(device0);
        updateMasterRelativeOrigin(device0);
    }

    ros::spinOnce();
    updateJoints(&device0->mech[0]);
    

    //Inverse Cable Coupling
    invCableCoupling(device0, currParams->runlevel);

    // Set all joints to zero torque
    _mech = NULL;  _joint = NULL;
    while (loop_over_joints(device0, _mech, _joint, i,j) )
    {
        if (currParams->runlevel != RL_PEDAL_DN)
        {
            _joint->tau_d=0;
        }
        else
        {
            mpos_PD_control(_joint);
        }

        TorqueToDAC(device0);
    }
    //    gravComp(device0);

    return 0;
}


/**
* raven_sinusoidal_joint_motion()
*    Applies a sinusoidal trajectory to all joints
*/
int raven_sinusoidal_joint_motion(struct device *device0, struct param_pass *currParams){
    static int controlStart = 0;
    static unsigned long int delay=0;
    const float f_period[8] = {6, 7, 10, 9999999, 10, 5, 10, 6};
//    const float f_magnitude[8] = {0 DEG2RAD, 0 DEG2RAD, 0.0, 9999999, 0 DEG2RAD, 25 DEG2RAD, 0 DEG2RAD, 0 DEG2RAD};
    const float f_magnitude[8] = {10 DEG2RAD, 10 DEG2RAD, 0.02, 9999999, 20 DEG2RAD, 10 DEG2RAD, 10 DEG2RAD, 10 DEG2RAD};

    // If we're not in pedal down or init.init then do nothing.
    if (! ( currParams->runlevel == RL_INIT && currParams->sublevel == SL_AUTO_INIT ))
    {
        controlStart = 0;
        delay = gTime;
        // Set all joints to zero torque, and mpos_d = mpos
        for (int i=0; i < NUM_MECH; i++)
        {
            for (int j = 0; j < MAX_DOF_PER_MECH; j++)
            {
                struct DOF* _joint =  &(device0->mech[i].joint[j]);
                _joint->mpos_d = _joint->mpos;
                _joint->jpos_d = _joint->jpos;
                _joint->tau_d = 0;
            }
        }
        return 0;
    }

    // Wait for amplifiers to power up
    if (gTime - delay < 800)
        return 0;

    // Set trajectory on all the joints
    for (int i=0; i < NUM_MECH; i++)
    {
        for (int j = 0; j < MAX_DOF_PER_MECH; j++)
        {
            struct DOF * _joint =  &(device0->mech[i].joint[j]);
            int sgn = 1;

            if (device0->mech[i].type == GREEN_ARM)
                sgn = -1;

            // initialize trajectory
            if (!controlStart)
                start_trajectory(_joint, (_joint->jpos + sgn*f_magnitude[j]), f_period[j]);

            // Get trajectory update
            update_sinusoid_position_trajectory(_joint);
        }
    }

    //Inverse Cable Coupling
    invCableCoupling(device0, currParams->runlevel);

    // Do PD control on all the joints
    for (int i=0; i < NUM_MECH; i++)
    {
        for (int j = 0; j < MAX_DOF_PER_MECH; j++)
        {
            struct DOF * _joint =  &(device0->mech[i].joint[j]);

            // Do PD control
            mpos_PD_control(_joint);
	    if (_joint->type == TOOL_ROT_GREEN || _joint->type == TOOL_ROT_GOLD)
		_joint->tau_d = 0;
        }
    }


    TorqueToDAC(device0);

    controlStart = 1;
    return 0;
}


/**
*  applyTorque()
*    For debugging robot.  Apply a set torque command (tau_d) to a joint.
*
*/
int applyTorque(struct device *device0, struct param_pass *currParams)
{
    // Only run in runlevel 1.2
    if ( ! (currParams->runlevel == RL_INIT && currParams->sublevel == SL_AUTO_INIT ))
        return 0;

    for (int i=0;i<NUM_MECH;i++)
    {
        for (int j=0;j<MAX_DOF_PER_MECH;j++)
        {
            if (device0->mech[i].type == GOLD_ARM)
            {
                device0->mech[i].joint[j].tau_d = (1.0/1000.0) * (float)(currParams->torque_vals[j]);  // convert from mNm to Nm
            }
            else
            {
                device0->mech[i].joint[j].tau_d = (1.0/1000.0) * (float)(currParams->torque_vals[MAX_DOF_PER_MECH+j]);
            }
        }
    }
    // gravComp(device0);
    TorqueToDAC(device0);

    return 0;
}


/**
* raven_motor_position_control()
*     runs pd control on motor position
*/
int raven_motor_position_control(struct device *device0, struct param_pass *currParams)
{
    static int controlStart = 0;
    static unsigned long int delay=0;

    struct DOF *_joint = NULL;
    struct mechanism* _mech = NULL;
    int i=0,j=0;

    // If we're not in pedal down or init.init then do nothing.
    if (! ( currParams->runlevel == RL_PEDAL_DN ||
          ( currParams->runlevel == RL_INIT     && currParams->sublevel == SL_AUTO_INIT ))
       )
    {
        controlStart = 0;
        delay = gTime;

        // Set all joints to zero torque, and mpos_d = mpos
        _mech = NULL;  _joint = NULL;
        while (loop_over_joints(device0, _mech, _joint, i,j) )
        {
            _joint->mpos_d = _joint->mpos;
            _joint->tau_d = 0;
        }
        return 0;
    }

    if (gTime - delay < 800)
        return 0;

    // Set trajectory on all the joints
    _mech = NULL;  _joint = NULL;
    while (loop_over_joints(device0, _mech, _joint, i,j) )
    {

        // initialize trajectory
        if (!controlStart && _joint->type == Z_INS_GREEN)
            start_trajectory(_joint, 0.08, 8);


        else if (!controlStart)
            _joint->jpos_d = _joint->jpos;

        // Get trajectory update
        if (_joint->type == Z_INS_GREEN)
           update_sinusoid_position_trajectory(_joint);

    }

    //Inverse Cable Coupling
    invCableCoupling(device0, currParams->runlevel);

    // Do PD control on all the joints
    _mech = NULL;  _joint = NULL;
    while (loop_over_joints(device0, _mech, _joint, i,j) )
    {
        // Do PD control
        mpos_PD_control(_joint);

        if (device0->mech[i].type == GOLD_ARM)
            _joint->tau_d=0;
    }

    TorqueToDAC(device0);

    controlStart = 1;
    return 0;
}

/**
* raven_joint_velocity_control()
*     runs pi_control on joint velocity.
*/
int raven_joint_velocity_control(struct device *device0, struct param_pass *currParams)
{
    static int controlStart;
    static unsigned long int delay=0;

    // Run velocity control
    if ( currParams->runlevel == RL_PEDAL_DN ||
            ( currParams->runlevel == RL_INIT &&
              currParams->sublevel == SL_AUTO_INIT ))
    {
        // delay the start of control for 300ms b/c the amps have to turn on.
        if (gTime - delay < 800)
            return 0;

        for (int i=0; i < NUM_MECH; i++)
        {
            for (int j = 0; j < MAX_DOF_PER_MECH; j++)
            {
                struct DOF * _joint =  &(device0->mech[i].joint[j]);

                if (device0->mech[i].type == GOLD_ARM)
                {
                    // initialize velocity trajectory
                    if (!controlStart)
                        start_trajectory(_joint);

                    // Get the desired joint velocities
                    update_linear_sinusoid_velocity_trajectory(_joint);

                    // Run PI control
                    jvel_PI_control(_joint, !controlStart);

                }
                else
                {
                    _joint->tau_d = 0;
                }
            }
        }

        if (!controlStart)
            controlStart = 1;

        // Convert joint torque to DAC value.
        TorqueToDAC(device0);
    }

    else
    {
        delay=gTime;
        controlStart = 0;
        for (int i=0; i < NUM_MECH; i++)
            for (int j = 0; j < MAX_DOF_PER_MECH; j++)
            {
                device0->mech[i].joint[j].tau_d=0;
            }
        TorqueToDAC(device0);
    }

    return 0;
}





