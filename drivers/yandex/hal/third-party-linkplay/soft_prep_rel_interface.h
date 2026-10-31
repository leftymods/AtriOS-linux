#include "soft_prep_rel_define.h"

#ifndef SOFT_PREP_REL_INTERFACE_H
#define SOFT_PREP_REL_INTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif


/*******************************************************************************
**
** Function         Soft_Prep_Init
**
** Description      Initial software preprocess module
**                  called before any other functions of soft_prep are called.
**
** Returns          error code
**
*******************************************************************************/
int Soft_Prep_Init(SOFT_PREP_INIT init_input);

/*******************************************************************************
**
** Function         Soft_Prep_Deinit
**
** Description      Deinitial software preprocess module
**                  called to release software preprocess module.
**
** Returns          error code
**
*******************************************************************************/
int Soft_Prep_Deinit(void);


/*******************************************************************************
**
** Function         Soft_Prep_GetVersion
**
** Description      Get the version of software preprocess module
**
** Returns          string of version
**
*******************************************************************************/
char *Soft_Prep_GetVersion(void);

/*******************************************************************************
**
** Function         Soft_Prep_GetCapability
**
** Description      Get the capablity of software preprocess module
**
** Returns          Support capability defined in Soft_prep_rel_define.h
**
*******************************************************************************/
int Soft_Prep_GetCapability(void);

/*******************************************************************************
**
** Function         Soft_Prep_GetAudioParam
**
** Description      Get the audio output parameter of software preprocess module
**
** Returns          Support Audio format defined in Soft_prep_rel_define.h
**
*******************************************************************************/
WAVE_PARAM Soft_Prep_GetAudioParam(void);

/*******************************************************************************
**
** Function         Soft_Prep_GetMicrawAudioParam
**
** Description      Get the mic raw capture audio stream parameter of software preprocess module
**
** Returns          Support Audio format defined in Soft_prep_rel_define.h
**
*******************************************************************************/
WAVE_PARAM Soft_Prep_GetMicrawAudioParam(void);

/*******************************************************************************
**
** Function         Soft_prep_flushState
**
** Description      flush the statue of audio to software preprocess module
**                  need to call every time wake word detected. only usable when
**                  SOFP_PREP_CAP_WAKEUP unavailiable in Support Capability
**                  implement this behave will improve the perfermance of soft
**                  preprocess
**
** Returns          error code
**
*******************************************************************************/
int Soft_prep_flushState(void);

/*******************************************************************************
**
** Function         Soft_Prep_StartWWE
**
** Description      enable Wake Word Engine. Only usable when SOFP_PREP_CAP_WAKEUP
**                  availiable in Support capability
**
** Returns          error code
**
*******************************************************************************/
int Soft_Prep_StartWWE(void);

/*******************************************************************************
**
** Function         Soft_Prep_StopWWE
**
** Description      disable Wake Word Engine. Only usable when SOFP_PREP_CAP_WAKEUP
**                  availiable in Support capability
**
** Returns          error code
**
*******************************************************************************/
int Soft_Prep_StopWWE(void);

/*******************************************************************************
**
** Function         Soft_Prep_StartCapture
**
** Description      enable audio capture
**
** Returns          error code
**
*******************************************************************************/
int Soft_Prep_StartCapture(void);

/*******************************************************************************
**
** Function         Soft_Prep_StopCapture
**
** Description      disable audio capture
**
** Returns          error code
**
*******************************************************************************/
int Soft_Prep_StopCapture(void);


#ifdef __cplusplus
}
#endif
#endif //SOFT_PREP_REL_INTERFACE_H
