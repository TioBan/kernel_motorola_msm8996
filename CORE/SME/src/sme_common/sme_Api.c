/*
 * Copyright (c) 2012-2016 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */


/**=========================================================================

  \file  smeApi.c

  \brief Definitions for SME APIs

  ========================================================================*/

/*===========================================================================

                      EDIT HISTORY FOR FILE


  This section contains comments describing changes made to the module.
  Notice that changes are listed in reverse chronological order.



  when           who                 what, where, why
----------       ---                --------------------------------------------------------
06/03/10     js                     Added support to hostapd driven
 *                                  deauth/disassoc/mic failure

===========================================================================*/

/*--------------------------------------------------------------------------
  Include Files
  ------------------------------------------------------------------------*/

#include "smsDebug.h"
#include "sme_Api.h"
#include "csrInsideApi.h"
#include "smeInside.h"
#include "csrInternal.h"
#include "wlan_qct_wda.h"
#include "halMsgApi.h"
#include "vos_trace.h"
#include "sme_Trace.h"
#include "vos_types.h"
#include "vos_trace.h"
#include "vos_utils.h"
#include "sapApi.h"
#include "macTrace.h"
#ifdef WLAN_FEATURE_NAN
#include "nan_Api.h"
#endif
#include "regdomain_common.h"
#include "schApi.h"
#include "sme_nan_datapath.h"

extern tSirRetStatus uMacPostCtrlMsg(void* pSirGlobal, tSirMbMsg* pMb);

#define LOG_SIZE 256
#define READ_MEMORY_DUMP_CMD     9
#define TL_INIT_STATE            0

static tSelfRecoveryStats gSelfRecoveryStats;


// TxMB Functions
extern eHalStatus pmcPrepareCommand( tpAniSirGlobal pMac, tANI_U32 sessionId,
                                     eSmeCommandType cmdType, void *pvParam,
                                     tANI_U32 size, tSmeCmd **ppCmd);
extern void pmcReleaseCommand( tpAniSirGlobal pMac, tSmeCmd *pCommand );
extern void qosReleaseCommand( tpAniSirGlobal pMac, tSmeCmd *pCommand );
extern void csr_release_roc_req_cmd(tpAniSirGlobal mac_ctx);
extern eHalStatus p2pProcessRemainOnChannelCmd(tpAniSirGlobal pMac, tSmeCmd *p2pRemainonChn);
extern eHalStatus sme_remainOnChnRsp( tpAniSirGlobal pMac, tANI_U8 *pMsg);
extern eHalStatus sme_remainOnChnReady( tHalHandle hHal, tANI_U8* pMsg);
extern eHalStatus sme_sendActionCnf( tHalHandle hHal, tANI_U8* pMsg);
extern eHalStatus p2pProcessNoAReq(tpAniSirGlobal pMac, tSmeCmd *pNoACmd);

static eHalStatus initSmeCmdList(tpAniSirGlobal pMac);
static void smeAbortCommand( tpAniSirGlobal pMac, tSmeCmd *pCommand, tANI_BOOLEAN fStopping );

eCsrPhyMode sme_GetPhyMode(tHalHandle hHal);

eHalStatus sme_HandleChangeCountryCode(tpAniSirGlobal pMac,  void *pMsgBuf);

void sme_DisconnectConnectedSessions(tpAniSirGlobal pMac);

eHalStatus sme_HandleGenericChangeCountryCode(tpAniSirGlobal pMac,  void *pMsgBuf);

eHalStatus sme_HandlePreChannelSwitchInd(tHalHandle hHal, void *pMsgBuf);

eHalStatus sme_HandlePostChannelSwitchInd(tHalHandle hHal);

#if defined(FEATURE_WLAN_ESE) && defined(FEATURE_WLAN_ESE_UPLOAD)
tANI_BOOLEAN csrIsSupportedChannel(tpAniSirGlobal pMac, tANI_U8 channelId);
#endif

#ifdef WLAN_FEATURE_11W
eHalStatus sme_UnprotectedMgmtFrmInd( tHalHandle hHal,
                                      tpSirSmeUnprotMgmtFrameInd pSmeMgmtFrm );
#endif

/* Message processor for events from DFS */
eHalStatus dfsMsgProcessor(tpAniSirGlobal pMac,
                           v_U16_t msg_type,void *pMsgBuf);

/* Channel Change Response Indication Handler */
eHalStatus sme_ProcessChannelChangeResp(tpAniSirGlobal pMac,
                           v_U16_t msg_type,void *pMsgBuf);
eHalStatus sme_process_set_max_tx_power(tpAniSirGlobal pMac,
						tSmeCmd *command);

//Internal SME APIs
eHalStatus sme_AcquireGlobalLock( tSmeStruct *psSme)
{
    eHalStatus status = eHAL_STATUS_INVALID_PARAMETER;

    if(psSme)
    {
        if( VOS_IS_STATUS_SUCCESS( vos_lock_acquire( &psSme->lkSmeGlobalLock) ) )
        {
            status = eHAL_STATUS_SUCCESS;
        }
    }

    return (status);
}


eHalStatus sme_ReleaseGlobalLock( tSmeStruct *psSme)
{
    eHalStatus status = eHAL_STATUS_INVALID_PARAMETER;

    if(psSme)
    {
        if( VOS_IS_STATUS_SUCCESS( vos_lock_release( &psSme->lkSmeGlobalLock) ) )
        {
            status = eHAL_STATUS_SUCCESS;
        }
    }

    return (status);
}

/**
 * free_sme_cmds() - This function frees memory allocated for SME commands
 * @mac_ctx:      Pointer to Global MAC structure
 *
 * This function frees memory allocated for SME commands
 *
 * @Return: void
 */
static void free_sme_cmds(tpAniSirGlobal mac_ctx)
{
	uint32_t idx;
	if (NULL == mac_ctx->sme.pSmeCmdBufAddr)
		return;

	for (idx = 0; idx < mac_ctx->sme.totalSmeCmd; idx++)
		vos_mem_free(mac_ctx->sme.pSmeCmdBufAddr[idx]);

	vos_mem_free(mac_ctx->sme.pSmeCmdBufAddr);
	mac_ctx->sme.pSmeCmdBufAddr = NULL;
}

static eHalStatus initSmeCmdList(tpAniSirGlobal pMac)
{
    eHalStatus status;
    tSmeCmd *pCmd;
    tANI_U32 cmd_idx;
    uint32_t sme_cmd_ptr_ary_sz;
    VOS_STATUS vosStatus;
    vos_timer_t* cmdTimeoutTimer = NULL;

    pMac->sme.totalSmeCmd = SME_TOTAL_COMMAND;
    if (!HAL_STATUS_SUCCESS(status = csrLLOpen(pMac->hHdd,
                           &pMac->sme.smeCmdActiveList)))
       goto end;

    if (!HAL_STATUS_SUCCESS(status = csrLLOpen(pMac->hHdd,
                           &pMac->sme.smeCmdPendingList)))
       goto end;

    if (!HAL_STATUS_SUCCESS(status = csrLLOpen(pMac->hHdd,
                           &pMac->sme.smeScanCmdActiveList)))
       goto end;

    if (!HAL_STATUS_SUCCESS(status = csrLLOpen(pMac->hHdd,
                                            &pMac->sme.smeScanCmdPendingList)))
       goto end;

    if (!HAL_STATUS_SUCCESS(status = csrLLOpen(pMac->hHdd,
                                             &pMac->sme.smeCmdFreeList)))
       goto end;

    /* following pointer contains array of pointers for tSmeCmd* */
    sme_cmd_ptr_ary_sz = sizeof(void*) * pMac->sme.totalSmeCmd;
    pMac->sme.pSmeCmdBufAddr = vos_mem_malloc(sme_cmd_ptr_ary_sz);
    if (NULL == pMac->sme.pSmeCmdBufAddr) {
        status = eHAL_STATUS_FAILURE;
        goto end;
    }

    status = eHAL_STATUS_SUCCESS;
    vos_mem_set(pMac->sme.pSmeCmdBufAddr, sme_cmd_ptr_ary_sz, 0);
    for (cmd_idx = 0; cmd_idx < pMac->sme.totalSmeCmd; cmd_idx++) {
       /*
        * Since total size of all commands together can be huge chunk of
        * memory, allocate SME cmd individually. These SME CMDs are moved
        * between pending and active queues. And these freeing of these
        * queues just manipulates the list but does not actually frees SME
        * CMD pointers. Hence store each SME CMD address in the array,
        * sme.pSmeCmdBufAddr. This will later facilitate freeing up of all
        * SME CMDs with just a for loop.
        */
        pMac->sme.pSmeCmdBufAddr[cmd_idx] = vos_mem_malloc(sizeof(tSmeCmd));
        if (NULL == pMac->sme.pSmeCmdBufAddr[cmd_idx]) {
           status = eHAL_STATUS_FAILURE;
           free_sme_cmds(pMac);
           goto end;
        }
        pCmd = (tSmeCmd*)pMac->sme.pSmeCmdBufAddr[cmd_idx];
        csrLLInsertTail(&pMac->sme.smeCmdFreeList, &pCmd->Link, LL_ACCESS_LOCK);
    }

    /* This timer is only to debug the active list command timeout */

    cmdTimeoutTimer = (vos_timer_t*)vos_mem_malloc(sizeof(vos_timer_t));
    if (cmdTimeoutTimer)
    {
        pMac->sme.smeCmdActiveList.cmdTimeoutTimer = cmdTimeoutTimer;
       vosStatus =
            vos_timer_init( pMac->sme.smeCmdActiveList.cmdTimeoutTimer,
                              VOS_TIMER_TYPE_SW,
                              activeListCmdTimeoutHandle,
                              (void*) pMac);

        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "Init Timer fail for active list command process time out");
            vos_mem_free(pMac->sme.smeCmdActiveList.cmdTimeoutTimer);
        }
        else
        {
            pMac->sme.smeCmdActiveList.cmdTimeoutDuration =
                CSR_ACTIVE_LIST_CMD_TIMEOUT_VALUE;
        }
    }

end:
    if (!HAL_STATUS_SUCCESS(status))
       smsLog(pMac, LOGE, "failed to initialize sme command list:%d\n",
              status);

    return (status);
}


void smeReleaseCommand(tpAniSirGlobal pMac, tSmeCmd *pCmd)
{
    pCmd->command = eSmeNoCommand;
    csrLLInsertTail(&pMac->sme.smeCmdFreeList, &pCmd->Link, LL_ACCESS_LOCK);
}



static void smeReleaseCmdList(tpAniSirGlobal pMac, tDblLinkList *pList)
{
    tListElem *pEntry;
    tSmeCmd *pCommand;

    while((pEntry = csrLLRemoveHead(pList, LL_ACCESS_LOCK)) != NULL)
    {
        //TODO: base on command type to call release functions
        //reinitialize different command types so they can be reused
        pCommand = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
        smeAbortCommand(pMac, pCommand, eANI_BOOLEAN_TRUE);
    }
}

static void purgeSmeCmdList(tpAniSirGlobal pMac)
{
    //release any out standing commands back to free command list
    smeReleaseCmdList(pMac, &pMac->sme.smeCmdPendingList);
    smeReleaseCmdList(pMac, &pMac->sme.smeCmdActiveList);
    smeReleaseCmdList(pMac, &pMac->sme.smeScanCmdPendingList);
    smeReleaseCmdList(pMac, &pMac->sme.smeScanCmdActiveList);
}

void purgeSmeSessionCmdList(tpAniSirGlobal pMac, tANI_U32 sessionId,
        tDblLinkList *pList)
{
    //release any out standing commands back to free command list
    tListElem *pEntry, *pNext;
    tSmeCmd *pCommand;
    tDblLinkList localList;

    vos_mem_zero(&localList, sizeof(tDblLinkList));
    if(!HAL_STATUS_SUCCESS(csrLLOpen(pMac->hHdd, &localList)))
    {
        smsLog(pMac, LOGE, FL(" failed to open list"));
        return;
    }

    csrLLLock(pList);
    pEntry = csrLLPeekHead(pList, LL_ACCESS_NOLOCK);
    while(pEntry != NULL)
    {
        pNext = csrLLNext(pList, pEntry, LL_ACCESS_NOLOCK);
        pCommand = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
        if(pCommand->sessionId == sessionId)
        {
            if(csrLLRemoveEntry(pList, pEntry, LL_ACCESS_NOLOCK))
            {
                csrLLInsertTail(&localList, pEntry, LL_ACCESS_NOLOCK);
            }
        }
        pEntry = pNext;
    }
    csrLLUnlock(pList);

    while( (pEntry = csrLLRemoveHead(&localList, LL_ACCESS_NOLOCK)) )
    {
        pCommand = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
        smeAbortCommand(pMac, pCommand, eANI_BOOLEAN_TRUE);
    }
    csrLLClose(&localList);
}


static eHalStatus freeSmeCmdList(tpAniSirGlobal pMac)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    purgeSmeCmdList(pMac);
    csrLLClose(&pMac->sme.smeCmdPendingList);
    csrLLClose(&pMac->sme.smeCmdActiveList);
    csrLLClose(&pMac->sme.smeScanCmdPendingList);
    csrLLClose(&pMac->sme.smeScanCmdActiveList);
    csrLLClose(&pMac->sme.smeCmdFreeList);

    /*destroy active list command time out timer */
    vos_timer_destroy(pMac->sme.smeCmdActiveList.cmdTimeoutTimer);
    vos_mem_free(pMac->sme.smeCmdActiveList.cmdTimeoutTimer);
    pMac->sme.smeCmdActiveList.cmdTimeoutTimer = NULL;

    status = vos_lock_acquire(&pMac->sme.lkSmeGlobalLock);
    if(status != eHAL_STATUS_SUCCESS)
    {
        smsLog(pMac, LOGE,
            FL("Failed to acquire the lock status = %d"), status);
        goto done;
    }

    free_sme_cmds(pMac);

    status = vos_lock_release(&pMac->sme.lkSmeGlobalLock);
    if(status != eHAL_STATUS_SUCCESS)
    {
        smsLog(pMac, LOGE,
            FL("Failed to release the lock status = %d"), status);
    }
done:
    return (status);
}


void dumpCsrCommandInfo(tpAniSirGlobal pMac, tSmeCmd *pCmd)
{
    switch( pCmd->command )
    {
    case eSmeCommandScan:
        smsLog( pMac, LOGE, " scan command reason is %d", pCmd->u.scanCmd.reason );
        break;

    case eSmeCommandRoam:
        smsLog( pMac, LOGE, " roam command reason is %d", pCmd->u.roamCmd.roamReason );
        break;

    case eSmeCommandWmStatusChange:
        smsLog( pMac, LOGE, " WMStatusChange command type is %d", pCmd->u.wmStatusChangeCmd.Type );
        break;

    case eSmeCommandSetKey:
        smsLog( pMac, LOGE, " setKey command auth(%d) enc(%d)",
                        pCmd->u.setKeyCmd.authType, pCmd->u.setKeyCmd.encType );
        break;

    case eSmeCommandRemoveKey:
        smsLog( pMac, LOGE, " removeKey command auth(%d) enc(%d)",
                        pCmd->u.removeKeyCmd.authType, pCmd->u.removeKeyCmd.encType );
        break;

    default:
        smsLog( pMac, LOGE, " default: Unhandled command %d",
                pCmd->command);
        break;
    }
}

tSmeCmd *smeGetCommandBuffer( tpAniSirGlobal pMac )
{
    tSmeCmd *pRetCmd = NULL, *pTempCmd = NULL;
    tListElem *pEntry;
    static int smeCommandQueueFull = 0;

    pEntry = csrLLRemoveHead( &pMac->sme.smeCmdFreeList, LL_ACCESS_LOCK );

    /*
     * If we can get another MS Msg buffer, then we are ok.  Just link
     * the entry onto the linked list.  (We are using the linked list
     * to keep track of the message buffers).
     */
    if ( pEntry )
    {
        pRetCmd = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
        /* reset when free list is available */
        smeCommandQueueFull = 0;
    }
    else
    {
        int idx = 1;

        //Cannot change pRetCmd here since it needs to return later.
        pEntry = csrLLPeekHead( &pMac->sme.smeCmdActiveList, LL_ACCESS_LOCK );
        if( pEntry )
        {
           pTempCmd = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
        }
        smsLog( pMac, LOGE, "Out of command buffer.... command (0x%X) stuck",
           (pTempCmd) ? pTempCmd->command : eSmeNoCommand );
        if(pTempCmd)
        {
            if( eSmeCsrCommandMask & pTempCmd->command )
            {
                //CSR command is stuck. See what the reason code is for that command
                dumpCsrCommandInfo(pMac, pTempCmd);
            }
        } //if(pTempCmd)

        //dump what is in the pending queue
        csrLLLock(&pMac->sme.smeCmdPendingList);
        pEntry = csrLLPeekHead( &pMac->sme.smeCmdPendingList, LL_ACCESS_NOLOCK );
        while(pEntry && !smeCommandQueueFull)
        {
            pTempCmd = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
            /* Print only 1st five commands from pending queue. */
            if (idx <= 5)
                smsLog( pMac, LOGE, "Out of command buffer.... SME pending command #%d (0x%X)",
                        idx, pTempCmd->command );
            idx++;
            if( eSmeCsrCommandMask & pTempCmd->command )
            {
                //CSR command is stuck. See what the reason code is for that command
                dumpCsrCommandInfo(pMac, pTempCmd);
            }
            pEntry = csrLLNext( &pMac->sme.smeCmdPendingList, pEntry, LL_ACCESS_NOLOCK );
        }
        csrLLUnlock(&pMac->sme.smeCmdPendingList);

        idx = 1;
        //There may be some more command in CSR's own pending queue
        csrLLLock(&pMac->roam.roamCmdPendingList);
        pEntry = csrLLPeekHead( &pMac->roam.roamCmdPendingList, LL_ACCESS_NOLOCK );
        while(pEntry && !smeCommandQueueFull)
        {
            pTempCmd = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
            /* Print only 1st five commands from CSR pending queue */
            if (idx <= 5)
                smsLog( pMac, LOGE,
                     "Out of command buffer.... CSR roamCmdPendingList command #%d (0x%X)",
                     idx, pTempCmd->command );
            idx++;
            dumpCsrCommandInfo(pMac, pTempCmd);
            pEntry = csrLLNext( &pMac->roam.roamCmdPendingList, pEntry, LL_ACCESS_NOLOCK );
        }
        /* Increament static variable so that it prints pending command only once*/
        smeCommandQueueFull++;
        csrLLUnlock(&pMac->roam.roamCmdPendingList);

        /* panic with out-of-command */
        if (pMac->roam.configParam.enable_fatal_event) {
            vos_flush_logs(WLAN_LOG_TYPE_FATAL,
                           WLAN_LOG_INDICATOR_HOST_DRIVER,
                           WLAN_LOG_REASON_SME_OUT_OF_CMD_BUF,
                           false);
        } else {
            /* Trigger SSR */
            vos_wlanRestart();
        }
    }

    /* memset to zero */
    if (pRetCmd) {
        vos_mem_set((tANI_U8 *)&pRetCmd->command, sizeof(pRetCmd->command), 0);
        vos_mem_set((tANI_U8 *)&pRetCmd->sessionId,
                    sizeof(pRetCmd->sessionId), 0);
        vos_mem_set((tANI_U8 *)&pRetCmd->u, sizeof(pRetCmd->u), 0);
    }

    return(pRetCmd);
}


void smePushCommand( tpAniSirGlobal pMac, tSmeCmd *pCmd, tANI_BOOLEAN fHighPriority )
{
    if (!SME_IS_START(pMac)) {
        smsLog(pMac, LOGE, FL("Sme in stop state"));
        return;
    }

    if ( fHighPriority )
    {
        csrLLInsertHead( &pMac->sme.smeCmdPendingList, &pCmd->Link, LL_ACCESS_LOCK );
    }
    else
    {
        csrLLInsertTail( &pMac->sme.smeCmdPendingList, &pCmd->Link, LL_ACCESS_LOCK );
    }

    // process the command queue...
    smeProcessPendingQueue( pMac );

    return;
}


static eSmeCommandType smeIsFullPowerNeeded( tpAniSirGlobal pMac, tSmeCmd *pCommand )
{
    eSmeCommandType pmcCommand = eSmeNoCommand;
    tANI_BOOLEAN fFullPowerNeeded = eANI_BOOLEAN_FALSE;
    tPmcState pmcState;
    eHalStatus status;

    do
    {
        pmcState = pmcGetPmcState(pMac);

        status = csrIsFullPowerNeeded( pMac, pCommand, NULL, &fFullPowerNeeded );
        if( !HAL_STATUS_SUCCESS(status) )
        {
            //PMC state is not right for the command, drop it
            return ( eSmeDropCommand );
        }
        if( fFullPowerNeeded  ) break;
        fFullPowerNeeded = ( ( eSmeCommandAddTs == pCommand->command ) ||
                    ( eSmeCommandDelTs ==  pCommand->command ) );
        if( fFullPowerNeeded ) break;
#ifdef FEATURE_OEM_DATA_SUPPORT
        fFullPowerNeeded = (pmcState == IMPS &&
                                       eSmeCommandOemDataReq == pCommand->command);
        if(fFullPowerNeeded) break;
#endif
        fFullPowerNeeded = (pmcState == IMPS &&
                            eSmeCommandRemainOnChannel == pCommand->command);
        if(fFullPowerNeeded) break;
    } while(0);

    if( fFullPowerNeeded )
    {
        switch( pmcState )
        {
        case IMPS:
        case STANDBY:
            pmcCommand = eSmeCommandExitImps;
            break;

        case BMPS:
            pmcCommand = eSmeCommandExitBmps;
            break;

        case UAPSD:
            pmcCommand = eSmeCommandExitUapsd;
            break;

        case WOWL:
            pmcCommand = eSmeCommandExitWowl;
            break;

        default:
            break;
        }
    }

    return ( pmcCommand );
}

static eSmeCommandType smePsOffloadIsFullPowerNeeded(tpAniSirGlobal pMac,
                                                     tSmeCmd *pCommand)
{
    eSmeCommandType pmcCommand = eSmeNoCommand;
    tANI_BOOLEAN fFullPowerNeeded = eANI_BOOLEAN_FALSE;
    eHalStatus status;
    tPmcState pmcState;

    do
    {
        /* Check for CSR Commands which require full power */
        status = csrPsOffloadIsFullPowerNeeded(pMac, pCommand, NULL,
                                               &fFullPowerNeeded);
        if(!HAL_STATUS_SUCCESS(status))
        {
            /* PMC state is not right for the command, drop it */
            return eSmeDropCommand;
        }
        if(fFullPowerNeeded) break;

        /* Check for SME Commands which require Full Power */
        if((eSmeCommandAddTs == pCommand->command) ||
           ((eSmeCommandDelTs ==  pCommand->command)))
        {
           /* Get the PMC State */
           pmcState = pmcOffloadGetPmcState(pMac, pCommand->sessionId);
           switch(pmcState)
           {
               case REQUEST_BMPS:
               case BMPS:
               case REQUEST_START_UAPSD:
               case REQUEST_STOP_UAPSD:
               case UAPSD:
                   fFullPowerNeeded = eANI_BOOLEAN_TRUE;
                 break;
               case STOPPED:
                 break;
               default:
                 break;
           }
           break;
        }
    } while(0);

    if(fFullPowerNeeded)
    {
        pmcCommand = eSmeCommandExitBmps;
    }

    return pmcCommand;
}


//For commands that need to do extra cleanup.
static void smeAbortCommand( tpAniSirGlobal pMac, tSmeCmd *pCommand, tANI_BOOLEAN fStopping )
{
    if( eSmePmcCommandMask & pCommand->command )
    {
        if(!pMac->psOffloadEnabled)
            pmcAbortCommand( pMac, pCommand, fStopping );
        else
            pmcOffloadAbortCommand(pMac, pCommand, fStopping);
    }
    else if ( eSmeCsrCommandMask & pCommand->command )
    {
        csrAbortCommand( pMac, pCommand, fStopping );
    }
    else
    {
        switch( pCommand->command )
        {
            case eSmeCommandRemainOnChannel:
                if (NULL != pCommand->u.remainChlCmd.callback)
                {
                    remainOnChanCallback callback =
                                            pCommand->u.remainChlCmd.callback;
                    /* process the msg */
                    if( callback )
                    {
                        callback(pMac, pCommand->u.remainChlCmd.callbackCtx,
                                            eCSR_SCAN_ABORT );
                    }
                }
                smeReleaseCommand( pMac, pCommand );
                break;
            default:
                smeReleaseCommand( pMac, pCommand );
                break;
        }
    }
}

tListElem *csrGetCmdToProcess(tpAniSirGlobal pMac, tDblLinkList *pList,
                              tANI_U8 sessionId, tANI_BOOLEAN fInterlocked)
{
    tListElem *pCurEntry = NULL;
    tSmeCmd *pCommand;

    /* Go through the list and return the command whose session id is not
     * matching with the current ongoing scan cmd sessionId */
    pCurEntry = csrLLPeekHead( pList, LL_ACCESS_LOCK );
    while (pCurEntry)
    {
        pCommand = GET_BASE_ADDR(pCurEntry, tSmeCmd, Link);
        if (pCommand->sessionId != sessionId)
        {
            smsLog(pMac, LOG1, "selected the command with different sessionId");
            return pCurEntry;
        }

        pCurEntry = csrLLNext(pList, pCurEntry, fInterlocked);
    }

    smsLog(pMac, LOG1, "No command pending with different sessionId");
    return NULL;
}

tANI_BOOLEAN smeProcessScanQueue(tpAniSirGlobal pMac)
{
    tListElem *pEntry;
    tSmeCmd *pCommand;
    tListElem *pSmeEntry = NULL;
    tSmeCmd *pSmeCommand = NULL;
    tANI_BOOLEAN status = eANI_BOOLEAN_TRUE;

    if ((!csrLLIsListEmpty(&pMac->sme.smeCmdActiveList, LL_ACCESS_LOCK ))) {
        pSmeEntry = csrLLPeekHead(&pMac->sme.smeCmdActiveList,
                        LL_ACCESS_LOCK);
        if (pSmeEntry)
            pSmeCommand = GET_BASE_ADDR(pSmeEntry, tSmeCmd, Link) ;
    }

    csrLLLock( &pMac->sme.smeScanCmdActiveList );
    if (csrLLIsListEmpty( &pMac->sme.smeScanCmdActiveList,
                LL_ACCESS_NOLOCK ))
    {
        if (!csrLLIsListEmpty(&pMac->sme.smeScanCmdPendingList,
                    LL_ACCESS_LOCK))
        {
            pEntry = csrLLPeekHead( &pMac->sme.smeScanCmdPendingList,
                    LL_ACCESS_LOCK );
            if (pEntry) {
                pCommand = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
                if (pSmeCommand != NULL) {
                    /* if there is an active SME command, do not process
                     * the pending scan cmd
                     */
                    smsLog(pMac, LOG1, "SME scan cmd is pending on session %d",
                           pSmeCommand->sessionId);
                    status = eANI_BOOLEAN_FALSE;
                    goto end;
                }
                //We cannot execute any command in wait-for-key state until setKey is through.
                if (CSR_IS_WAIT_FOR_KEY( pMac, pCommand->sessionId))
                {
                    if (!CSR_IS_SET_KEY_COMMAND(pCommand))
                    {
                        smsLog(pMac, LOGE,
                                "  Cannot process command(%d) while waiting for key",
                                pCommand->command);
                        status = eANI_BOOLEAN_FALSE;
                        goto end;
                    }
                }
                if ( csrLLRemoveEntry( &pMac->sme.smeScanCmdPendingList,
                            pEntry, LL_ACCESS_LOCK ) )
                {
                    csrLLInsertHead( &pMac->sme.smeScanCmdActiveList,
                            &pCommand->Link, LL_ACCESS_NOLOCK );

                    switch (pCommand->command)
                    {
                        case eSmeCommandScan:
                            smsLog(pMac, LOG1,
                                    " Processing scan offload command ");
                            csrProcessScanCommand( pMac, pCommand );
                            break;
                        case eSmeCommandRemainOnChannel:
                            smsLog(pMac, LOG1,
                                    "Processing req remain on channel offload"
                                    " command");
                            p2pProcessRemainOnChannelCmd(pMac, pCommand);
                            break;
                        default:
                            smsLog(pMac, LOGE,
                                    " Something wrong, wrong command enqueued"
                                    " to smeScanCmdPendingList");
                            pEntry = csrLLRemoveHead(
                                    &pMac->sme.smeScanCmdActiveList,
                                    LL_ACCESS_NOLOCK );
                            pCommand = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
                            smeReleaseCommand( pMac, pCommand );
                            break;
                    }
                }
            }
        }
    }
end:
    csrLLUnlock(&pMac->sme.smeScanCmdActiveList);
    return status;
}

tANI_BOOLEAN smeProcessCommand( tpAniSirGlobal pMac )
{
    tANI_BOOLEAN fContinue = eANI_BOOLEAN_FALSE;
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tListElem *pEntry;
    tSmeCmd *pCommand;
    tListElem *pSmeEntry;
    tSmeCmd *pSmeCommand;
    eSmeCommandType pmcCommand = eSmeNoCommand;

    /*
     * If the ActiveList is empty, then nothing is active so we can process a
     * pending command.
     * Always lock active list before locking pending list.
     */
    csrLLLock( &pMac->sme.smeCmdActiveList );
    if ( csrLLIsListEmpty( &pMac->sme.smeCmdActiveList, LL_ACCESS_NOLOCK ) )
    {
        if(!csrLLIsListEmpty(&pMac->sme.smeCmdPendingList, LL_ACCESS_LOCK))
        {
            /* If scan command is pending in the smeScanCmdActive list
             * then pick the command from smeCmdPendingList which is
             * not matching with the scan command session id.
             * At any point of time only one command will be allowed
             * on a single session. */
            if ((pMac->fScanOffload) &&
                    (!csrLLIsListEmpty(&pMac->sme.smeScanCmdActiveList,
                                       LL_ACCESS_LOCK)))
            {
                pSmeEntry = csrLLPeekHead(&pMac->sme.smeScanCmdActiveList,
                        LL_ACCESS_LOCK);
                if (pSmeEntry)
                {
                    pSmeCommand = GET_BASE_ADDR(pSmeEntry, tSmeCmd, Link);

                    pEntry = csrGetCmdToProcess(pMac,
                            &pMac->sme.smeCmdPendingList,
                            pSmeCommand->sessionId,
                            LL_ACCESS_LOCK);
                    goto sme_process_cmd;
                }
            }

            //Peek the command
            pEntry = csrLLPeekHead( &pMac->sme.smeCmdPendingList, LL_ACCESS_LOCK );
sme_process_cmd:
            if( pEntry )
            {
                pCommand = GET_BASE_ADDR( pEntry, tSmeCmd, Link );

                /* Allow only disconnect command
                 * in wait-for-key state until setKey is through.
                 */
                if( CSR_IS_WAIT_FOR_KEY( pMac, pCommand->sessionId ) &&
                    !CSR_IS_DISCONNECT_COMMAND( pCommand ) )
                {
                    if (CSR_IS_CLOSE_SESSION_COMMAND(pCommand)) {
                        tSmeCmd *sme_cmd = NULL;

                        smsLog(pMac, LOGE,
                                 FL("SessionId %d: close session command issued while waiting for key, issue disconnect first"),
                                 pCommand->sessionId);
                        status = csr_prepare_disconnect_command(pMac,
                                             pCommand->sessionId, &sme_cmd);
                        if (HAL_STATUS_SUCCESS(status) && sme_cmd) {
                            csrLLLock(&pMac->sme.smeCmdPendingList);
                            csrLLInsertHead(&pMac->sme.smeCmdPendingList,
                                              &sme_cmd->Link, LL_ACCESS_NOLOCK);
                            pEntry = csrLLPeekHead(&pMac->sme.smeCmdPendingList,
                                                      LL_ACCESS_NOLOCK);
                            csrLLUnlock(&pMac->sme.smeCmdPendingList);
                            goto sme_process_cmd;
                        }
                    }

                    if( !CSR_IS_SET_KEY_COMMAND( pCommand ) )
                    {
                        csrLLUnlock( &pMac->sme.smeCmdActiveList );
                        smsLog(pMac, LOGE, FL("SessionId %d:  Cannot process "
                               "command(%d) while waiting for key"),
                               pCommand->sessionId, pCommand->command);
                        fContinue = eANI_BOOLEAN_FALSE;
                        goto sme_process_scan_queue;
                    }
                }
                if(pMac->psOffloadEnabled)
                    pmcCommand = smePsOffloadIsFullPowerNeeded(pMac, pCommand);
                else
                   pmcCommand = smeIsFullPowerNeeded( pMac, pCommand );
                if( eSmeDropCommand == pmcCommand )
                {
                    csrLLUnlock(&pMac->sme.smeCmdActiveList);
                    //This command is not ok for current PMC state
                    if( csrLLRemoveEntry( &pMac->sme.smeCmdPendingList, pEntry, LL_ACCESS_LOCK ) )
                    {
                        smeAbortCommand( pMac, pCommand, eANI_BOOLEAN_FALSE );
                    }
                    //tell caller to continue
                    fContinue = eANI_BOOLEAN_TRUE;
                    goto sme_process_scan_queue;
                }
                else if( eSmeNoCommand != pmcCommand )
                {
                    tExitBmpsInfo exitBmpsInfo;
                    void *pv = NULL;
                    tANI_U32 size = 0;
                    tSmeCmd *pPmcCmd = NULL;

                    if( eSmeCommandExitBmps == pmcCommand )
                    {
                        exitBmpsInfo.exitBmpsReason = eSME_REASON_OTHER;
                        pv = (void *)&exitBmpsInfo;
                        size = sizeof(tExitBmpsInfo);
                    }
                    //pmcCommand has to be one of the exit power save command
                    status = pmcPrepareCommand(pMac, pCommand->sessionId,
                                               pmcCommand, pv, size,
                                               &pPmcCmd);
                    if( HAL_STATUS_SUCCESS( status ) && pPmcCmd )
                    {
                        //Force this command to wake up the chip
                        csrLLInsertHead( &pMac->sme.smeCmdActiveList, &pPmcCmd->Link, LL_ACCESS_NOLOCK );
                        MTRACE(vos_trace(VOS_MODULE_ID_SME,
                           TRACE_CODE_SME_COMMAND, pPmcCmd->sessionId, pPmcCmd->command));
                        csrLLUnlock( &pMac->sme.smeCmdActiveList );
                        /* Handle PS Offload Case Separately */
                        if(pMac->psOffloadEnabled)
                        {
                           fContinue = pmcOffloadProcessCommand(pMac, pPmcCmd);
                        }
                        else
                        {
                            fContinue = pmcProcessCommand( pMac, pPmcCmd );
                        }
                        if( fContinue )
                        {
                            //The command failed, remove it
                            if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList, &pPmcCmd->Link, LL_ACCESS_NOLOCK ) )
                            {
                                pmcReleaseCommand( pMac, pPmcCmd );
                            }
                        }
                    }
                    else
                    {
                        csrLLUnlock( &pMac->sme.smeCmdActiveList );
                        smsLog( pMac, LOGE, FL(  "Cannot issue command(0x%X) to wake up the chip. Status = %d"), pmcCommand, status );
                        //Let it retry
                        fContinue = eANI_BOOLEAN_TRUE;
                    }
                    goto sme_process_scan_queue;
                }
                if ( csrLLRemoveEntry( &pMac->sme.smeCmdPendingList, pEntry, LL_ACCESS_LOCK ) )
                {
                    // we can reuse the pCommand

                    // Insert the command onto the ActiveList...
                    csrLLInsertHead( &pMac->sme.smeCmdActiveList, &pCommand->Link, LL_ACCESS_NOLOCK );

                    // .... and process the command.

                    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                       TRACE_CODE_SME_COMMAND, pCommand->sessionId, pCommand->command));
                    switch ( pCommand->command )
                    {

                        case eSmeCommandScan:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            status = csrProcessScanCommand( pMac, pCommand );
                            break;

                        case eSmeCommandRoam:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            status  = csrRoamProcessCommand( pMac, pCommand );
                            if(!HAL_STATUS_SUCCESS(status))
                            {
                                if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList,
                                            &pCommand->Link, LL_ACCESS_LOCK ) )
                                {
                                    csrReleaseCommandRoam( pMac, pCommand );
                                }
                            }
                            break;

                        case eSmeCommandWmStatusChange:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            csrRoamProcessWmStatusChangeCommand(pMac, pCommand);
                            break;

                        case eSmeCommandSetKey:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            status = csrRoamProcessSetKeyCommand( pMac, pCommand );
                            if(!HAL_STATUS_SUCCESS(status))
                            {
                                if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList,
                                            &pCommand->Link, LL_ACCESS_LOCK ) )
                                {
                                    csrReleaseCommandSetKey( pMac, pCommand );
                                }
                            }
                            break;

                        case eSmeCommandRemoveKey:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            status = csrRoamProcessRemoveKeyCommand( pMac, pCommand );
                            if(!HAL_STATUS_SUCCESS(status))
                            {
                                if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList,
                                            &pCommand->Link, LL_ACCESS_LOCK ) )
                                {
                                    csrReleaseCommandRemoveKey( pMac, pCommand );
                                }
                            }
                            break;

                        case eSmeCommandAddStaSession:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            csrProcessAddStaSessionCommand( pMac, pCommand );
                            break;
                        case eSmeCommandNdpInitiatorRequest:
                            csrLLUnlock(&pMac->sme.smeCmdActiveList);
                            if (csr_process_ndp_initiator_request(pMac,
                                        pCommand) != eHAL_STATUS_SUCCESS) {
                                if (csrLLRemoveEntry(
                                        &pMac->sme.smeCmdActiveList,
                                        &pCommand->Link, LL_ACCESS_LOCK)) {
                                    csrReleaseCommand(pMac, pCommand);
                                }
                            }
                            break;
                        case eSmeCommandNdpResponderRequest:
                            csrLLUnlock(&pMac->sme.smeCmdActiveList);
                            status =
                                 csr_process_ndp_responder_request(pMac,
                                                                   pCommand);
                            if (!HAL_STATUS_SUCCESS(status)) {
                                if (csrLLRemoveEntry(
                                          &pMac->sme.smeCmdActiveList,
                                             &pCommand->Link, LL_ACCESS_LOCK))
                                    csrReleaseCommand(pMac, pCommand);
                            }
                            break;
                        case eSmeCommandDelStaSession:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            csrProcessDelStaSessionCommand( pMac, pCommand );
                            break;
                        case eSmeCommandSetMaxTxPower:
                            csrLLUnlock(&pMac->sme.smeCmdActiveList);
                            sme_process_set_max_tx_power(pMac, pCommand);
                            /* We need to re-run the command */
                            fContinue = eANI_BOOLEAN_TRUE;
                            /* No Rsp expected, free cmd from active list */
                            if(csrLLRemoveEntry(&pMac->sme.smeCmdActiveList,
                                        &pCommand->Link, LL_ACCESS_LOCK)) {
                               csrReleaseCommand(pMac, pCommand);
                            }
                            break;

#ifdef FEATURE_OEM_DATA_SUPPORT
                        case eSmeCommandOemDataReq:
                            csrLLUnlock(&pMac->sme.smeCmdActiveList);
                            oemData_ProcessOemDataReqCommand(pMac, pCommand);
                            break;
#endif
                        case eSmeCommandRemainOnChannel:
                            csrLLUnlock(&pMac->sme.smeCmdActiveList);
                            p2pProcessRemainOnChannelCmd(pMac, pCommand);
                            break;
                        case eSmeCommandNoAUpdate:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            p2pProcessNoAReq(pMac,pCommand);
                        case eSmeCommandEnterImps:
                        case eSmeCommandExitImps:
                        case eSmeCommandEnterBmps:
                        case eSmeCommandExitBmps:
                        case eSmeCommandEnterUapsd:
                        case eSmeCommandExitUapsd:
                        case eSmeCommandEnterWowl:
                        case eSmeCommandExitWowl:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            if(pMac->psOffloadEnabled)
                            {
                                fContinue =
                                  pmcOffloadProcessCommand(pMac, pCommand);
                            }
                            else
                            {
                                fContinue = pmcProcessCommand(pMac, pCommand);
                            }
                            if( fContinue )
                            {
                                //The command failed, remove it
                                if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList,
                                            &pCommand->Link, LL_ACCESS_LOCK ) )
                                {
                                    pmcReleaseCommand( pMac, pCommand );
                                }
                            }
                            break;

                        //Treat standby differently here because caller may not be able to handle
                        //the failure so we do our best here
                        case eSmeCommandEnterStandby:
                            if( csrIsConnStateDisconnected( pMac, pCommand->sessionId ) )
                            {
                                //It can continue
                                csrLLUnlock( &pMac->sme.smeCmdActiveList );
                                if(pMac->psOffloadEnabled)
                                {
                                    fContinue =
                                        pmcOffloadProcessCommand(pMac, pCommand);
                                }
                                else
                                {
                                    fContinue =
                                        pmcProcessCommand(pMac, pCommand);
                                }
                                if( fContinue )
                                {
                                    //The command failed, remove it
                                    if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList,
                                                &pCommand->Link, LL_ACCESS_LOCK ) )
                                    {
                                        pmcReleaseCommand( pMac, pCommand );
                                    }
                                }
                            }
                            else
                            {
                                //Need to issue a disconnect first before processing this command
                                tSmeCmd *pNewCmd;

                                //We need to re-run the command
                                fContinue = eANI_BOOLEAN_TRUE;
                                //Pull off the standby command first
                                if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList,
                                                &pCommand->Link, LL_ACCESS_NOLOCK ) )
                                {
                                    csrLLUnlock( &pMac->sme.smeCmdActiveList );
                                    //Need to call CSR function here because the disconnect command
                                    //is handled by CSR
                                    pNewCmd = csrGetCommandBuffer( pMac );
                                    if( NULL != pNewCmd )
                                    {
                                        //Put the standby command to the head of the pending list first
                                        csrLLInsertHead( &pMac->sme.smeCmdPendingList, &pCommand->Link,
                                                        LL_ACCESS_LOCK );
                                        pNewCmd->command = eSmeCommandRoam;
                                        pNewCmd->u.roamCmd.roamReason = eCsrForcedDisassoc;
                                        //Put the disassoc command before the standby command
                                        csrLLInsertHead( &pMac->sme.smeCmdPendingList, &pNewCmd->Link,
                                                        LL_ACCESS_LOCK );
                                    }
                                    else
                                    {
                                        //Continue the command here
                                        if(pMac->psOffloadEnabled)
                                        {
                                            fContinue =
                                                 pmcOffloadProcessCommand(pMac,
                                                                     pCommand);
                                        }
                                        else
                                        {
                                            fContinue = pmcProcessCommand(pMac,
                                                                     pCommand);
                                        }
                                        if( fContinue )
                                        {
                                            //The command failed, remove it
                                            if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList,
                                                        &pCommand->Link, LL_ACCESS_LOCK ) )
                                            {
                                                pmcReleaseCommand( pMac, pCommand );
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    csrLLUnlock( &pMac->sme.smeCmdActiveList );
                                    smsLog( pMac, LOGE, FL(" failed to remove standby command") );
                                    VOS_ASSERT(0);
                                }
                            }
                            break;

                        case eSmeCommandAddTs:
                        case eSmeCommandDelTs:
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
#ifndef WLAN_MDM_CODE_REDUCTION_OPT
                            fContinue = qosProcessCommand( pMac, pCommand );
                            if( fContinue )
                            {
                                //The command failed, remove it
                                if( csrLLRemoveEntry( &pMac->sme.smeCmdActiveList,
                                            &pCommand->Link, LL_ACCESS_NOLOCK ) )
                                {
//#ifndef WLAN_MDM_CODE_REDUCTION_OPT
                                    qosReleaseCommand( pMac, pCommand );
//#endif /* WLAN_MDM_CODE_REDUCTION_OPT*/
                                }
                            }
#endif
                            break;
#ifdef FEATURE_WLAN_TDLS
                        case eSmeCommandTdlsSendMgmt:
                        case eSmeCommandTdlsAddPeer:
                        case eSmeCommandTdlsDelPeer:
                        case eSmeCommandTdlsLinkEstablish:
                            {
                                VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                                        "sending TDLS Command 0x%x to PE", pCommand->command);

                                csrLLUnlock(&pMac->sme.smeCmdActiveList);
                                status = csrTdlsProcessCmd(pMac, pCommand);
                                if (!HAL_STATUS_SUCCESS(status)) {
                                    if (csrLLRemoveEntry(&pMac->sme.smeCmdActiveList,
                                                         &pCommand->Link,
                                                         LL_ACCESS_LOCK)) {
                                        vos_mem_zero(&pCommand->u.tdlsCmd,
                                                     sizeof(tTdlsCmd));
                                        csrReleaseCommand(pMac, pCommand);
                                    }
                                }
                            }
                            break ;
#endif

                        default:
                            //something is wrong
                            //remove it from the active list
                            smsLog(pMac, LOGE, " csrProcessCommand processes an unknown command %d", pCommand->command);
                            pEntry = csrLLRemoveHead( &pMac->sme.smeCmdActiveList, LL_ACCESS_NOLOCK );
                            csrLLUnlock( &pMac->sme.smeCmdActiveList );
                            pCommand = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
                            smeReleaseCommand( pMac, pCommand );
                            status = eHAL_STATUS_FAILURE;
                            break;
                    }
                    if(!HAL_STATUS_SUCCESS(status))
                    {
                        fContinue = eANI_BOOLEAN_TRUE;
                    }
                }//if(pEntry)
                else
                {
                    //This is odd. Some one else pull off the command.
                    csrLLUnlock( &pMac->sme.smeCmdActiveList );
                    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                                "Remove entry failed");
                }
            }
            else
            {
                csrLLUnlock( &pMac->sme.smeCmdActiveList );
                VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                                "Get Pending command failed");
            }
        }
        else
        {
            //No command waiting
            csrLLUnlock( &pMac->sme.smeCmdActiveList );
            /*
             * This is only used to restart an idle mode scan,
             * it means at least one other idle scan has finished.
             * Moreover Idle Scan is not supported with power
             * save offload supported
             */
            if(!pMac->psOffloadEnabled &&
               pMac->scan.fRestartIdleScan &&
               eANI_BOOLEAN_FALSE == pMac->scan.fCancelIdleScan)
            {
                tANI_U32 nTime = 0;

                pMac->scan.fRestartIdleScan = eANI_BOOLEAN_FALSE;
                if(!HAL_STATUS_SUCCESS(csrScanTriggerIdleScan(pMac, &nTime)))
                {
                    csrScanStartIdleScanTimer(pMac, nTime);
                }
            }
        }
    }
    else {
        csrLLUnlock( &pMac->sme.smeCmdActiveList );
    }

sme_process_scan_queue:
    if (pMac->fScanOffload && !(smeProcessScanQueue(pMac)))
        fContinue = eANI_BOOLEAN_FALSE;

    return ( fContinue );
}

void smeProcessPendingQueue( tpAniSirGlobal pMac )
{
    while( smeProcessCommand( pMac ) );
}


tANI_BOOLEAN smeCommandPending(tpAniSirGlobal pMac)
{
    return ( !csrLLIsListEmpty( &pMac->sme.smeCmdActiveList, LL_ACCESS_NOLOCK ) ||
        !csrLLIsListEmpty(&pMac->sme.smeCmdPendingList, LL_ACCESS_NOLOCK) );
}

/**
 * sme_get_sessionid_from_activelist() - gets session id
 * @mac: mac context
 *
 * This function is used to get session id from sme command
 * active list
 *
 * Return: returns session id
 */
uint32_t sme_get_sessionid_from_activelist(tpAniSirGlobal mac)
{
	tListElem *entry;
	tSmeCmd *command;
	uint32_t session_id = 0;

	entry = csrLLPeekHead(&mac->sme.smeCmdActiveList, LL_ACCESS_LOCK);
	if (entry) {
		command = GET_BASE_ADDR(entry, tSmeCmd, Link);
		session_id = command->sessionId;
	}

	return session_id;
}

/**
 * sme_state_info_dump() - prints state information of sme layer
 * @buf: buffer pointer
 * @size: size of buffer to be filled
 *
 * This function is used to dump state information of sme layer
 *
 * Return: None
 */
static void sme_state_info_dump(char **buf_ptr, uint16_t *size)
{
	uint32_t session_id;
	tHalHandle hal;
	tpAniSirGlobal mac;
	v_CONTEXT_t vos_ctx_ptr;
	uint16_t len = 0;
	char *buf = *buf_ptr;

	/* get the global voss context */
	vos_ctx_ptr = vos_get_global_context(VOS_MODULE_ID_VOSS, NULL);

	if (NULL == vos_ctx_ptr) {
		VOS_ASSERT(0);
		return;
	}

	hal = vos_get_context(VOS_MODULE_ID_SME, vos_ctx_ptr);
	if (NULL == hal) {
		VOS_ASSERT(0);
		return;
	}

	mac = PMAC_STRUCT(hal);
	smsLog(mac, LOG1, FL("size of buffer: %d"), *size);

	session_id = sme_get_sessionid_from_activelist(mac);

	len += vos_scnprintf(buf + len, *size - len,
		"\n active command sessionid %d", session_id);
	len += vos_scnprintf(buf + len, *size - len,
		"\n NeighborRoamState: %d",
		mac->roam.neighborRoamInfo[session_id].neighborRoamState);
	len += vos_scnprintf(buf + len, *size - len,
		"\n RoamState: %d", mac->roam.curState[session_id]);
	len += vos_scnprintf(buf + len, *size - len,
		"\n RoamSubState: %d", mac->roam.curSubState[session_id]);
	len += vos_scnprintf(buf + len, *size - len,
		"\n ConnectState: %d",
		mac->roam.roamSession[session_id].connectState);
	len += vos_scnprintf(buf + len, *size - len,
		"\n pmcState: %d", mac->pmc.pmcState);
	len += vos_scnprintf(buf + len, *size - len,
		"\n PmmState: %d", mac->pmm.gPmmState);

	*size -= len;
	*buf_ptr += len;
}

/**
 * sme_register_debug_callback() - registration function sme layer
 * to print sme state information
 *
 * Return: None
 */
static void sme_register_debug_callback(void)
{
	vos_register_debug_callback(VOS_MODULE_ID_SME, &sme_state_info_dump);
}

//Global APIs

/*--------------------------------------------------------------------------

  \brief sme_Open() - Initialize all SME modules and put them at idle state

  The function initializes each module inside SME, PMC, CCM, CSR, etc. . Upon
  successfully return, all modules are at idle state ready to start.

  smeOpen must be called before any other SME APIs can be involved.
  smeOpen must be called after macOpen.
  This is a synchronous call
  \param hHal - The handle returned by macOpen.

  \return eHAL_STATUS_SUCCESS - SME is successfully initialized.

          Other status means SME is failed to be initialized
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_Open(tHalHandle hHal)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
#ifndef WLAN_FEATURE_MBSSID
   v_PVOID_t pvosGCtx = vos_get_global_context(VOS_MODULE_ID_SAP, NULL);
#endif

   do {
      pMac->sme.state = SME_STATE_STOP;
      pMac->sme.currDeviceMode = VOS_STA_MODE;
      if( !VOS_IS_STATUS_SUCCESS( vos_lock_init( &pMac->sme.lkSmeGlobalLock ) ) )
      {
          smsLog( pMac, LOGE, "sme_Open failed init lock" );
          status = eHAL_STATUS_FAILURE;
          break;
      }

      status = ccmOpen(hHal);
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
         smsLog( pMac, LOGE,
                 "ccmOpen failed during initialization with status=%d", status );
         break;
      }

      status = csrOpen(pMac);
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
         smsLog( pMac, LOGE,
                 "csrOpen failed during initialization with status=%d", status );
         break;
      }

      if(!pMac->psOffloadEnabled)
      {
          status = pmcOpen(hHal);
          if ( ! HAL_STATUS_SUCCESS( status ) ) {
             smsLog( pMac, LOGE,
                 "pmcOpen failed during initialization with status=%d",
                 status );
             break;
          }
      }
      else
      {
          status = pmcOffloadOpen(hHal);
          if (! HAL_STATUS_SUCCESS(status)) {
             smsLog( pMac, LOGE,
                 "pmcOffloadOpen failed during initialization with status=%d",
                 status );
             break;
          }
      }

#ifdef FEATURE_WLAN_TDLS
      pMac->isTdlsPowerSaveProhibited = 0;
#endif

#ifndef WLAN_MDM_CODE_REDUCTION_OPT
      status = sme_QosOpen(pMac);
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
         smsLog( pMac, LOGE,
                 "Qos open failed during initialization with status=%d", status );
         break;
      }

      status = btcOpen(pMac);
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
         smsLog( pMac, LOGE,
                 "btcOpen open failed during initialization with status=%d", status );
         break;
      }
#endif
#ifdef FEATURE_OEM_DATA_SUPPORT
      status = oemData_OemDataReqOpen(pMac);
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
         smsLog(pMac, LOGE,
                "oemData_OemDataReqOpen failed during initialization with status=%d", status );
         break;
      }
#endif

      if(!HAL_STATUS_SUCCESS((status = initSmeCmdList(pMac))))
          break;

#ifndef WLAN_FEATURE_MBSSID
      if ( NULL == pvosGCtx ){
         smsLog( pMac, LOGE, "WLANSAP_Open open failed during initialization");
         status = eHAL_STATUS_FAILURE;
         break;
      }

      status = WLANSAP_Open( pvosGCtx );
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
          smsLog( pMac, LOGE,
                  "WLANSAP_Open open failed during initialization with status=%d", status );
          break;
      }
#endif

#if defined WLAN_FEATURE_VOWIFI
      status = rrmOpen(pMac);
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
         smsLog( pMac, LOGE,
                 "rrmOpen open failed during initialization with status=%d", status );
         break;
      }
#endif

      sme_p2pOpen(pMac);
      smeTraceInit(pMac);
      sme_register_debug_callback();

   }while (0);

   return status;
}

/*
 * sme_init_chan_list, triggers channel setup based on country code.
 */
eHalStatus sme_init_chan_list(tHalHandle hal, v_U8_t *alpha2,
                              COUNTRY_CODE_SOURCE cc_src)
{
    tpAniSirGlobal pmac = PMAC_STRUCT(hal);

    if ((cc_src == COUNTRY_CODE_SET_BY_USER) &&
        (pmac->roam.configParam.fSupplicantCountryCodeHasPriority))
    {
        pmac->roam.configParam.Is11dSupportEnabled = eANI_BOOLEAN_FALSE;
    }

    return csr_init_chan_list(pmac, alpha2);
}

/*--------------------------------------------------------------------------

  \brief sme_set11dinfo() - Set the 11d information about valid channels
   and there power using information from nvRAM
   This function is called only for AP.

  This is a synchronous call

  \param hHal - The handle returned by macOpen.
  \Param pSmeConfigParams - a pointer to a caller allocated object of
  typedef struct _smeConfigParams.

  \return eHAL_STATUS_SUCCESS - SME update the config parameters successfully.

          Other status means SME is failed to update the config parameters.
  \sa
--------------------------------------------------------------------------*/

eHalStatus sme_set11dinfo(tHalHandle hHal,  tpSmeConfigParams pSmeConfigParams)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                      TRACE_CODE_SME_RX_HDD_MSG_SET_11DINFO, NO_SESSION, 0));
   if (NULL == pSmeConfigParams ) {
      smsLog( pMac, LOGE,
              "Empty config param structure for SME, nothing to update");
      return status;
   }

   status = csrSetChannels(hHal, &pSmeConfigParams->csrConfig );
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "csrChangeDefaultConfigParam failed with status=%d",
              status );
   }
    return status;
}

/*--------------------------------------------------------------------------

  \brief sme_getSoftApDomain() - Get the current regulatory domain of softAp.

  This is a synchronous call

  \param hHal - The handle returned by HostapdAdapter.
  \Param v_REGDOMAIN_t - The current Regulatory Domain requested for SoftAp.

  \return eHAL_STATUS_SUCCESS - SME successfully completed the request.

          Other status means, failed to get the current regulatory domain.
  \sa
--------------------------------------------------------------------------*/

eHalStatus sme_getSoftApDomain(tHalHandle hHal,  v_REGDOMAIN_t *domainIdSoftAp)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_MSG_GET_SOFTAP_DOMAIN, NO_SESSION, 0));
   if (NULL == domainIdSoftAp ) {
      smsLog( pMac, LOGE, "Uninitialized domain Id");
      return status;
   }

   *domainIdSoftAp = pMac->scan.domainIdCurrent;
   status = eHAL_STATUS_SUCCESS;

   return status;
}


eHalStatus sme_setRegInfo(tHalHandle hHal,  tANI_U8 *apCntryCode)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_MSG_SET_REGINFO, NO_SESSION, 0));
   if (NULL == apCntryCode ) {
      smsLog( pMac, LOGE, "Empty Country Code, nothing to update");
      return status;
   }

   status = csrSetRegInfo(hHal, apCntryCode );
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "csrSetRegInfo failed with status=%d",
              status );
   }
    return status;
}

#if defined(FEATURE_WLAN_ESE) && defined(FEATURE_WLAN_ESE_UPLOAD)
eHalStatus sme_SetPlmRequest(tHalHandle hHal, tpSirPlmReq pPlmReq)
{
    eHalStatus status;
    tANI_BOOLEAN ret = eANI_BOOLEAN_FALSE;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tANI_U8 ch_list[WNI_CFG_VALID_CHANNEL_LIST] = {0};
    tANI_U8 count, valid_count = 0;
    vos_msg_t msg;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme)))
    {
        tCsrRoamSession *pSession = CSR_GET_SESSION( pMac, pPlmReq->sessionId );

        if(!pSession)
        {
            smsLog(pMac, LOGE, FL("session %d not found"), pPlmReq->sessionId);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        if( !pSession->sessionActive )
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                        "%s Invalid Sessionid", __func__);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        if (pPlmReq->enable) {

           /* validating channel numbers */
           for (count = 0; count < pPlmReq->plmNumCh; count++) {

              ret = csrIsSupportedChannel(pMac, pPlmReq->plmChList[count]);
              if (ret && pPlmReq->plmChList[count] > 14)
              {
                  if (NV_CHANNEL_DFS ==
                       vos_nv_getChannelEnabledState(pPlmReq->plmChList[count]))
                  {
                      /* DFS channel is provided, no PLM bursts can be
                      * transmitted. Ignoring these channels.
                      */
                      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                                "%s DFS channel %d ignored for PLM", __func__,
                                pPlmReq->plmChList[count]);
                      continue;
                  }
              }
              else if (!ret)
              {
                   /* Not supported, ignore the channel */
                   VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                             "%s Unsupported channel %d ignored for PLM",
                             __func__, pPlmReq->plmChList[count]);
                   continue;
              }
              ch_list[valid_count] = pPlmReq->plmChList[count];
              valid_count++;
           } /* End of for () */

           /* Copying back the valid channel list to plm struct */
           vos_mem_set((void *)pPlmReq->plmChList, pPlmReq->plmNumCh, 0);
           if (valid_count)
              vos_mem_copy(pPlmReq->plmChList, ch_list, valid_count);
           /* All are invalid channels, FW need to send the PLM
           *  report with "incapable" bit set.
           */
           pPlmReq->plmNumCh = valid_count;
        } /* PLM START */

        msg.type     = WDA_SET_PLM_REQ;
        msg.reserved = 0;
        msg.bodyptr  = pPlmReq;

        if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      "%s: Not able to post WDA_SET_PLM_REQ message to WDA",
                      __func__);
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return (status);
}
#endif

#ifdef FEATURE_WLAN_SCAN_PNO
/*--------------------------------------------------------------------------

  \brief sme_UpdateChannelConfig() - Update channel configuration in RIVA.

  It is used at driver start up to inform RIVA of the default channel
  configuration.

  This is a synchronous call

  \param hHal - The handle returned by macOpen.

  \return eHAL_STATUS_SUCCESS - SME update the channel config successfully.

          Other status means SME is failed to update the channel config.
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_UpdateChannelConfig(tHalHandle hHal)
{
  tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

  MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_MSG_UPDATE_CHANNEL_CONFIG, NO_SESSION, 0));
  pmcUpdateScanParams( pMac, &(pMac->roam.configParam),
                      &pMac->scan.base20MHzChannels, FALSE);
  return eHAL_STATUS_SUCCESS;
}
#endif // FEATURE_WLAN_SCAN_PNLO

/*--------------------------------------------------------------------------

  \brief sme_UpdateConfig() - Change configurations for all SME modules

  The function updates some configuration for modules in SME, CCM, CSR, etc
  during SMEs close open sequence.

  Modules inside SME apply the new configuration at the next transaction.

  This is a synchronous call

  \param hHal - The handle returned by macOpen.
  \Param pSmeConfigParams - a pointer to a caller allocated object of
  typedef struct _smeConfigParams.

  \return eHAL_STATUS_SUCCESS - SME update the config parameters successfully.

          Other status means SME is failed to update the config parameters.
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_UpdateConfig(tHalHandle hHal, tpSmeConfigParams pSmeConfigParams)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                   TRACE_CODE_SME_RX_HDD_MSG_UPDATE_CONFIG, NO_SESSION, 0));
   if (NULL == pSmeConfigParams ) {
      smsLog( pMac, LOGE,
              "Empty config param structure for SME, nothing to update");
      return status;
   }

   status = csrChangeDefaultConfigParam(pMac, &pSmeConfigParams->csrConfig);

   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "csrChangeDefaultConfigParam failed with status=%d",
              status );
   }
#if defined WLAN_FEATURE_VOWIFI
   status = rrmChangeDefaultConfigParam(hHal, &pSmeConfigParams->rrmConfig);

   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "rrmChangeDefaultConfigParam failed with status=%d",
              status );
   }
#endif
   //For SOC, CFG is set before start
   //We don't want to apply global CFG in connect state because that may cause some side affect
   if(
      csrIsAllSessionDisconnected( pMac) )
   {
       csrSetGlobalCfgs(pMac);
   }

   /* update the directed scan offload setting */
   pMac->fScanOffload = pSmeConfigParams->fScanOffload;

   if (pMac->fScanOffload)
   {
       /*
        * If scan offload is enabled then lim has allow the sending of
        * scan request to firmware even in power save mode. The firmware has
        * to take care of exiting from power save mode
        */
       status = ccmCfgSetInt(hHal, WNI_CFG_SCAN_IN_POWERSAVE,
                   eANI_BOOLEAN_TRUE, NULL, eANI_BOOLEAN_FALSE);

       if (eHAL_STATUS_SUCCESS != status)
       {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                   "Could not pass on WNI_CFG_SCAN_IN_POWERSAVE to CCM");
       }
   }
   pMac->isCoalesingInIBSSAllowed =
         pSmeConfigParams->csrConfig.isCoalesingInIBSSAllowed;

   /* update the p2p listen offload setting */
   pMac->fP2pListenOffload = pSmeConfigParams->fP2pListenOffload;

   /* update p2p offload status */
   pMac->pnoOffload = pSmeConfigParams->pnoOffload;

   pMac->fEnableDebugLog = pSmeConfigParams->fEnableDebugLog;

   /* update interface configuration */
   pMac->sme.max_intf_count = pSmeConfigParams->max_intf_count;

   pMac->enable5gEBT = pSmeConfigParams->enable5gEBT;
   pMac->sme.enableSelfRecovery = pSmeConfigParams->enableSelfRecovery;

   pMac->f_sta_miracast_mcc_rest_time_val =
        pSmeConfigParams->f_sta_miracast_mcc_rest_time_val;

#ifdef FEATURE_AP_MCC_CH_AVOIDANCE
   pMac->sap.sap_channel_avoidance = pSmeConfigParams->sap_channel_avoidance;
#endif /* FEATURE_AP_MCC_CH_AVOIDANCE */

   pMac->f_prefer_non_dfs_on_radar =
                       pSmeConfigParams->f_prefer_non_dfs_on_radar;
   pMac->fine_time_meas_cap = pSmeConfigParams->fine_time_meas_cap;

   return status;
}

/**
 * sme_update_roam_params - Store/Update the roaming params
 * @hHal                    Handle for Hal layer
 * @session_id              SME Session ID
 * @roam_params_src         The source buffer to copy
 * @update_param            Type of parameter to be updated
 *
 * Return: Return the status of the updation.
 */
eHalStatus sme_update_roam_params(tHalHandle hHal,
	uint8_t session_id, struct roam_ext_params roam_params_src,
	int update_param)
{
	tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
	struct roam_ext_params *roam_params_dst;
	uint8_t i;

	roam_params_dst = &pMac->roam.configParam.roam_params;
	switch(update_param) {
	case REASON_ROAM_EXT_SCAN_PARAMS_CHANGED:
		roam_params_dst->raise_rssi_thresh_5g =
			roam_params_src.raise_rssi_thresh_5g;
		roam_params_dst->drop_rssi_thresh_5g =
			roam_params_src.drop_rssi_thresh_5g;
		roam_params_dst->raise_factor_5g=
			roam_params_src.raise_factor_5g;
		roam_params_dst->drop_factor_5g =
			roam_params_src.drop_factor_5g;
		roam_params_dst->max_raise_rssi_5g =
			roam_params_src.max_raise_rssi_5g;
		roam_params_dst->max_drop_rssi_5g=
			roam_params_src.max_drop_rssi_5g;
		roam_params_dst->alert_rssi_threshold =
			roam_params_src.alert_rssi_threshold;
		roam_params_dst->is_5g_pref_enabled = true;
		break;
	case REASON_ROAM_SET_SSID_ALLOWED:
		vos_mem_set(&roam_params_dst->ssid_allowed_list, 0,
				sizeof(tSirMacSSid) * MAX_SSID_ALLOWED_LIST);
		roam_params_dst->num_ssid_allowed_list=
			roam_params_src.num_ssid_allowed_list;
		for (i=0; i<roam_params_dst->num_ssid_allowed_list; i++) {
			roam_params_dst->ssid_allowed_list[i].length =
				roam_params_src.ssid_allowed_list[i].length;
			vos_mem_copy(roam_params_dst->ssid_allowed_list[i].ssId,
				roam_params_src.ssid_allowed_list[i].ssId,
				roam_params_dst->ssid_allowed_list[i].length);
		}
		break;
	case REASON_ROAM_SET_FAVORED_BSSID:
		vos_mem_set(&roam_params_dst->bssid_favored, 0,
			sizeof(tSirMacAddr) * MAX_BSSID_FAVORED);
		roam_params_dst->num_bssid_favored=
			roam_params_src.num_bssid_favored;
		for (i=0; i<roam_params_dst->num_bssid_favored; i++) {
			vos_mem_copy(&roam_params_dst->bssid_favored[i],
				&roam_params_src.bssid_favored[i],
				sizeof(tSirMacAddr));
			roam_params_dst->bssid_favored_factor[i] =
				roam_params_src.bssid_favored_factor[i];
		}
		break;
	case REASON_ROAM_SET_BLACKLIST_BSSID:
		vos_mem_set(&roam_params_dst->bssid_avoid_list, 0,
			sizeof(tSirMacAddr) * MAX_BSSID_AVOID_LIST);
		roam_params_dst->num_bssid_avoid_list =
			roam_params_src.num_bssid_avoid_list;
		for (i=0; i<roam_params_dst->num_bssid_avoid_list; i++) {
			vos_mem_copy(&roam_params_dst->bssid_avoid_list[i],
				&roam_params_src.bssid_avoid_list[i],
				sizeof(tSirMacAddr));
		}
		break;
	case REASON_ROAM_GOOD_RSSI_CHANGED:
		roam_params_dst->good_rssi_roam =
			roam_params_src.good_rssi_roam;
		break;
	default:
		break;
	}
	csrRoamOffloadScan(pMac, session_id,
		ROAM_SCAN_OFFLOAD_UPDATE_CFG, update_param);
	return 0;
}
#ifdef WLAN_FEATURE_GTK_OFFLOAD
void sme_ProcessGetGtkInfoRsp( tHalHandle hHal,
                            tpSirGtkOffloadGetInfoRspParams pGtkOffloadGetInfoRsp)
{
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if (NULL == pMac)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_FATAL,
           "%s: pMac is null", __func__);
       return ;
   }
   if (pMac->pmc.GtkOffloadGetInfoCB == NULL)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
           "%s: HDD callback is null", __func__);
       return ;
   }
   pMac->pmc.GtkOffloadGetInfoCB(pMac->pmc.GtkOffloadGetInfoCBContext,
                                 pGtkOffloadGetInfoRsp);
}
#endif

/*--------------------------------------------------------------------------

  \fn    - sme_ProcessReadyToSuspend
  \brief - On getting ready to suspend indication, this function calls
           callback registered (HDD callbacks) with SME to inform
           ready to suspend indication.

  \param hHal - Handle returned by macOpen.
         pReadyToSuspend - Parameter received along with ready to suspend
                           indication from WMA.

  \return None

  \sa

  --------------------------------------------------------------------------*/
void sme_ProcessReadyToSuspend( tHalHandle hHal,
                                tpSirReadyToSuspendInd pReadyToSuspend)
{
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if (NULL == pMac)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_FATAL,
           "%s: pMac is null", __func__);
       return ;
   }

   if (NULL != pMac->readyToSuspendCallback)
   {
       pMac->readyToSuspendCallback (pMac->readyToSuspendContext,
                                     pReadyToSuspend->suspended);
       pMac->readyToSuspendCallback = NULL;
   }
}

#ifdef WLAN_FEATURE_EXTWOW_SUPPORT
/*--------------------------------------------------------------------------

  \fn - sme_ProcessReadyToExtWoW
  \brief - On getting ready to Ext WoW indication, this function calls
             callback registered (HDD callbacks) with SME to inform
             ready to ExtWoW indication.

  \param hHal - Handle returned by macOpen.
   pReadyToExtWoW - Parameter received along with ready to Ext WoW
                                indication from WMA.

  \return None

  \sa

 --------------------------------------------------------------------------*/
void sme_ProcessReadyToExtWoW( tHalHandle hHal,
                                 tpSirReadyToExtWoWInd pReadyToExtWoW)
{
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if (NULL == pMac)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_FATAL,
             "%s: pMac is null", __func__);
       return ;
   }

   if (NULL != pMac->readyToExtWoWCallback)
   {
       pMac->readyToExtWoWCallback (pMac->readyToExtWoWContext,
                                    pReadyToExtWoW->status);
       pMac->readyToExtWoWCallback = NULL;
       pMac->readyToExtWoWContext = NULL;
   }

}
#endif

/* ---------------------------------------------------------------------------
    \fn sme_ChangeConfigParams
    \brief The SME API exposed for HDD to provide config params to SME during
    SMEs stop -> start sequence.

    If HDD changed the domain that will cause a reset. This function will
    provide the new set of 11d information for the new domain. Currently this
    API provides info regarding 11d only at reset but we can extend this for
    other params (PMC, QoS) which needs to be initialized again at reset.

    This is a synchronous call

    \param hHal - The handle returned by macOpen.

    \Param
    pUpdateConfigParam - a pointer to a structure (tCsrUpdateConfigParam) that
                currently provides 11d related information like Country code,
                Regulatory domain, valid channel list, Tx power per channel, a
                list with active/passive scan allowed per valid channel.

    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_ChangeConfigParams(tHalHandle hHal,
                                 tCsrUpdateConfigParam *pUpdateConfigParam)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if (NULL == pUpdateConfigParam ) {
      smsLog( pMac, LOGE,
              "Empty config param structure for SME, nothing to reset");
      return status;
   }

   status = csrChangeConfigParams(pMac, pUpdateConfigParam);

   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "csrUpdateConfigParam failed with status=%d",
              status );
   }

   return status;

}

/*--------------------------------------------------------------------------

  \brief sme_HDDReadyInd() - SME sends eWNI_SME_SYS_READY_IND to PE to inform
  that the NIC is ready to run.

  The function is called by HDD at the end of initialization stage so PE/HAL can
  enable the NIC to running state.

  This is a synchronous call
  \param hHal - The handle returned by macOpen.

  \return eHAL_STATUS_SUCCESS - eWNI_SME_SYS_READY_IND is sent to PE
                                successfully.

          Other status means SME failed to send the message to PE.
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_HDDReadyInd(tHalHandle hHal)
{
   tSirSmeReadyReq Msg;
   eHalStatus status = eHAL_STATUS_FAILURE;
   tPmcPowerState powerState;
   tPmcSwitchState hwWlanSwitchState;
   tPmcSwitchState swWlanSwitchState;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                  TRACE_CODE_SME_RX_HDD_MSG_HDDREADYIND, NO_SESSION, 0));
   do
   {

      Msg.messageType = eWNI_SME_SYS_READY_IND;
      Msg.length      = sizeof( tSirSmeReadyReq );

      if (eSIR_FAILURE != uMacPostCtrlMsg( hHal, (tSirMbMsg*)&Msg ))
      {
         status = eHAL_STATUS_SUCCESS;
      }
      else
      {
         smsLog( pMac, LOGE,
                 "uMacPostCtrlMsg failed to send eWNI_SME_SYS_READY_IND");
         break;
      }

      if(!pMac->psOffloadEnabled)
      {
         status = pmcQueryPowerState( hHal, &powerState,
                                     &hwWlanSwitchState, &swWlanSwitchState );
         if ( ! HAL_STATUS_SUCCESS( status ) )
         {
              smsLog( pMac, LOGE, "pmcQueryPowerState failed with status=%d",
                      status );
              break;
         }

         if ( (ePMC_SWITCH_OFF != hwWlanSwitchState) &&
              (ePMC_SWITCH_OFF != swWlanSwitchState) )
         {
             status = csrReady(pMac);
             if ( ! HAL_STATUS_SUCCESS( status ) )
             {
                 smsLog( pMac, LOGE, "csrReady failed with status=%d", status );
                 break;
             }
             status = pmcReady(hHal);
             if ( ! HAL_STATUS_SUCCESS( status ) )
             {
                 smsLog( pMac, LOGE, "pmcReady failed with status=%d", status );
                 break;
             }
#ifndef WLAN_MDM_CODE_REDUCTION_OPT
             if(VOS_STATUS_SUCCESS != btcReady(hHal))
             {
                 status = eHAL_STATUS_FAILURE;
                 smsLog( pMac, LOGE, "btcReady failed");
                 break;
             }
#endif

#if defined WLAN_FEATURE_VOWIFI
             if(VOS_STATUS_SUCCESS != rrmReady(hHal))
             {
                 status = eHAL_STATUS_FAILURE;
                 smsLog( pMac, LOGE, "rrmReady failed");
                 break;
             }
#endif
         }
      }
      else
      {
          status = csrReady(pMac);
          if (!HAL_STATUS_SUCCESS(status))
          {
              smsLog( pMac, LOGE, "csrReady failed with status=%d", status );
              break;
          }
#ifndef WLAN_MDM_CODE_REDUCTION_OPT
          if(VOS_STATUS_SUCCESS != btcReady(hHal))
          {
              status = eHAL_STATUS_FAILURE;
              smsLog( pMac, LOGE, "btcReady failed");
              break;
          }
#endif

#if defined WLAN_FEATURE_VOWIFI
          if(VOS_STATUS_SUCCESS != rrmReady(hHal))
          {
              status = eHAL_STATUS_FAILURE;
              smsLog( pMac, LOGE, "rrmReady failed");
              break;
          }
#endif
      }
      pMac->sme.state = SME_STATE_READY;
   } while( 0 );

   return status;
}

/*--------------------------------------------------------------------------

  \brief sme_Start() - Put all SME modules at ready state.

  The function starts each module in SME, PMC, CCM, CSR, etc. . Upon
  successfully return, all modules are ready to run.
  This is a synchronous call
  \param hHal - The handle returned by macOpen.

  \return eHAL_STATUS_SUCCESS - SME is ready.

          Other status means SME is failed to start
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_Start(tHalHandle hHal)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   do
   {
      status = csrStart(pMac);
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
         smsLog( pMac, LOGE, "csrStart failed during smeStart with status=%d",
                 status );
         break;
      }

      if(!pMac->psOffloadEnabled)
      {
          status = pmcStart(hHal);
          if ( ! HAL_STATUS_SUCCESS( status ) ) {
             smsLog( pMac, LOGE,
                     "pmcStart failed during smeStart with status=%d",
                     status );
             break;
          }
      }
      else
      {
          status = pmcOffloadStart(hHal);
          if ( ! HAL_STATUS_SUCCESS( status ) ) {
             smsLog( pMac, LOGE,
                     "pmcOffloadStart failed during smeStart with status=%d",
                     status );
             break;
          }
      }

#ifndef WLAN_FEATURE_MBSSID
      status = WLANSAP_Start(vos_get_global_context(VOS_MODULE_ID_SAP, NULL));
      if ( ! HAL_STATUS_SUCCESS( status ) ) {
         smsLog( pMac, LOGE, "WLANSAP_Start failed during smeStart with status=%d",
                 status );
         break;
      }
#endif

      pMac->sme.state = SME_STATE_START;
   }while (0);

   return status;
}


#ifdef WLAN_FEATURE_PACKET_FILTERING
/******************************************************************************
*
* Name: sme_PCFilterMatchCountResponseHandler
*
* Description:
*    Invoke Packet Coalescing Filter Match Count callback routine
*
* Parameters:
*    hHal - HAL handle for device
*    pMsg - Pointer to tRcvFltPktMatchRsp structure
*
* Returns: eHalStatus
*
******************************************************************************/
eHalStatus sme_PCFilterMatchCountResponseHandler(tHalHandle hHal, void* pMsg)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpSirRcvFltPktMatchRsp pRcvFltPktMatchRsp = (tpSirRcvFltPktMatchRsp)pMsg;

    if (NULL == pMsg)
    {
        smsLog(pMac, LOGE, "in %s msg ptr is NULL", __func__);
        status = eHAL_STATUS_FAILURE;
    }
    else
    {
        smsLog(pMac, LOG2, "SME: entering "
            "sme_FilterMatchCountResponseHandler");

        /* Call Packet Coalescing Filter Match Count callback routine. */
        if (pMac->pmc.FilterMatchCountCB != NULL)
           pMac->pmc.FilterMatchCountCB(pMac->pmc.FilterMatchCountCBContext,
                                          pRcvFltPktMatchRsp);

        smsLog(pMac, LOG1, "%s: status=0x%x", __func__,
               pRcvFltPktMatchRsp->status);

        pMac->pmc.FilterMatchCountCB = NULL;
        pMac->pmc.FilterMatchCountCBContext = NULL;
    }

    return(status);
}
#endif // WLAN_FEATURE_PACKET_FILTERING


#ifdef WLAN_FEATURE_11W
/*------------------------------------------------------------------
 *
 * Handle the unprotected management frame indication from LIM and
 * forward it to HDD.
 *
 *------------------------------------------------------------------*/

eHalStatus sme_UnprotectedMgmtFrmInd( tHalHandle hHal,
                                      tpSirSmeUnprotMgmtFrameInd pSmeMgmtFrm)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus  status = eHAL_STATUS_SUCCESS;
    tCsrRoamInfo pRoamInfo = {0};
    tANI_U32 SessionId = pSmeMgmtFrm->sessionId;

    pRoamInfo.nFrameLength = pSmeMgmtFrm->frameLen;
    pRoamInfo.pbFrames = pSmeMgmtFrm->frameBuf;
    pRoamInfo.frameType = pSmeMgmtFrm->frameType;

    /* forward the mgmt frame to HDD */
    csrRoamCallCallback(pMac, SessionId, &pRoamInfo, 0, eCSR_ROAM_UNPROT_MGMT_FRAME_IND, 0);

    return status;
}
#endif

/*------------------------------------------------------------------
 *
 * Handle the DFS Radar Event and indicate it to the SAP
 *
 *------------------------------------------------------------------*/
eHalStatus dfsMsgProcessor(tpAniSirGlobal pMac, v_U16_t msgType, void *pMsgBuf)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tCsrRoamInfo roamInfo = {0};
    tSirSmeDfsEventInd *dfs_event;
    tSirSmeCSAIeTxCompleteRsp *csaIeTxCompleteRsp;
    tANI_U32 sessionId = 0;
    eRoamCmdStatus roamStatus;
    eCsrRoamResult roamResult;
    int i;

    switch (msgType)
    {
      case eWNI_SME_DFS_RADAR_FOUND:
      {
         /* Radar found !! */
         dfs_event = (tSirSmeDfsEventInd *)pMsgBuf;
         if (NULL == dfs_event)
         {
            smsLog(pMac, LOGE,
                   "%s: pMsg is NULL for eWNI_SME_DFS_RADAR_FOUND message",
                   __func__);
            return eHAL_STATUS_FAILURE;
         }
         sessionId = dfs_event->sessionId;
         roamInfo.dfs_event.sessionId = sessionId;
         roamInfo.dfs_event.chan_list.nchannels =
             dfs_event->chan_list.nchannels;
         for (i = 0; i < dfs_event->chan_list.nchannels; i++)
         {
             roamInfo.dfs_event.chan_list.channels[i] =
                 dfs_event->chan_list.channels[i];
         }

         roamInfo.dfs_event.dfs_radar_status = dfs_event->dfs_radar_status;
         roamInfo.dfs_event.use_nol = dfs_event->use_nol;

         roamStatus = eCSR_ROAM_DFS_RADAR_IND;
         roamResult = eCSR_ROAM_RESULT_DFS_RADAR_FOUND_IND;
         VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO_MED,
                   "sapdfs: Radar indication event occurred");
         break;
      }
      case eWNI_SME_DFS_CSAIE_TX_COMPLETE_IND:
      {
         csaIeTxCompleteRsp = (tSirSmeCSAIeTxCompleteRsp *)pMsgBuf;
         if (NULL == csaIeTxCompleteRsp)
         {
            smsLog(pMac, LOGE,
                   "%s: pMsg is NULL for eWNI_SME_DFS_CSAIE_TX_COMPLETE_IND",
                   __func__);
            return eHAL_STATUS_FAILURE;
         }
         sessionId = csaIeTxCompleteRsp->sessionId;
         roamStatus = eCSR_ROAM_DFS_CHAN_SW_NOTIFY;
         roamResult = eCSR_ROAM_RESULT_DFS_CHANSW_UPDATE_SUCCESS;
         VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO_MED,
         "sapdfs: Received eWNI_SME_DFS_CSAIE_TX_COMPLETE_IND for session id [%d]",
                   sessionId );
         break;
      }
      default:
      {
         smsLog(pMac, LOG1, "%s: Invalid DFS message = 0x%x", __func__,
                msgType);
         status = eHAL_STATUS_FAILURE;
         return status;
      }
    }

    /* Indicate Radar Event to SAP */
    csrRoamCallCallback(pMac, sessionId, &roamInfo, 0,
                        roamStatus, roamResult);
    return status;
}

/**
 * sme_extended_change_channel_ind()- function to indicate ECSA
 * action frame is received in lim to SAP
 * @mac_ctx:  pointer to global mac structure
 * @msg_buf: contain new channel and session id.
 *
 * This function is called to post ECSA action frame
 * receive event to SAP.
 *
 * Return: success if msg indicated to SAP else return failure
 */
static eHalStatus sme_extended_change_channel_ind(tpAniSirGlobal mac_ctx,
						void *msg_buf)
{
	struct sir_sme_ext_cng_chan_ind *ext_chan_ind;
	eHalStatus status = eHAL_STATUS_SUCCESS;
	uint32_t session_id = 0;
	tCsrRoamInfo roamInfo = {0};
	eRoamCmdStatus roamStatus;
	eCsrRoamResult roamResult;


	ext_chan_ind = msg_buf;
	if (NULL == ext_chan_ind) {
		smsLog(mac_ctx, LOGE,
			FL("pMsg is NULL for eWNI_SME_EXT_CHANGE_CHANNEL_IND"));
		return eHAL_STATUS_FAILURE;
	}
	session_id = ext_chan_ind->session_id;
	roamInfo.target_channel = ext_chan_ind->new_channel;
	roamStatus = eCSR_ROAM_EXT_CHG_CHNL_IND;
	roamResult = eCSR_ROAM_EXT_CHG_CHNL_UPDATE_IND;
	smsLog(mac_ctx, LOG1,
		FL("sapdfs: Received eWNI_SME_EXT_CHANGE_CHANNEL_IND for session id [%d]"),
		session_id);

	/* Indicate Ext Channel Change event to SAP */
	csrRoamCallCallback(mac_ctx, session_id, &roamInfo, 0,
					roamStatus, roamResult);
	return status;
}

#if defined(FEATURE_WLAN_ESE) && defined(FEATURE_WLAN_ESE_UPLOAD)
/*------------------------------------------------------------------
 *
 * Handle the tsm ie indication from  LIM and forward it to HDD.
 *
 *------------------------------------------------------------------*/
eHalStatus sme_TsmIeInd(tHalHandle hHal, tSirSmeTsmIEInd *pSmeTsmIeInd)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus     status = eHAL_STATUS_SUCCESS;
    tCsrRoamInfo   pRoamInfo = {0};
    tANI_U32       SessionId = pSmeTsmIeInd->sessionId;
    pRoamInfo.tsmIe.tsid= pSmeTsmIeInd->tsmIe.tsid;
    pRoamInfo.tsmIe.state= pSmeTsmIeInd->tsmIe.state;
    pRoamInfo.tsmIe.msmt_interval= pSmeTsmIeInd->tsmIe.msmt_interval;
    /* forward the tsm ie information to HDD */
    csrRoamCallCallback(pMac,
                        SessionId,
                        &pRoamInfo,
                        0,
                        eCSR_ROAM_TSM_IE_IND,
                        0);
    return status;
}
/* ---------------------------------------------------------------------------
    \fn sme_SetCCKMIe
    \brief  function to store the CCKM IE passed from supplicant and use
    it while packing reassociation request
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  pCckmIe - pointer to CCKM IE data
    \param  pCckmIeLen - length of the CCKM IE
    \- return Success or failure
    -------------------------------------------------------------------------*/
eHalStatus sme_SetCCKMIe(tHalHandle hHal, tANI_U8 sessionId,
                              tANI_U8 *pCckmIe, tANI_U8 cckmIeLen)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus     status  = eHAL_STATUS_SUCCESS;
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        csrSetCCKMIe(pMac, sessionId, pCckmIe, cckmIeLen);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_SetEseBeaconRequest
    \brief  function to set ESE beacon request parameters
    \param  hHal       - HAL handle for device
    \param  sessionId  - Session id
    \param  pEseBcnReq - pointer to ESE beacon request
    \- return Success or failure
    -------------------------------------------------------------------------*/
eHalStatus sme_SetEseBeaconRequest(tHalHandle hHal, const tANI_U8 sessionId,
                                   const tCsrEseBeaconReq* pEseBcnReq)
{
   eHalStatus               status           = eSIR_SUCCESS;
   tpAniSirGlobal           pMac             = PMAC_STRUCT( hHal );
   tpSirBeaconReportReqInd  pSmeBcnReportReq = NULL;
   tCsrEseBeaconReqParams  *pBeaconReq       = NULL;
   tANI_U8                  counter          = 0;
   tCsrRoamSession         *pSession         = CSR_GET_SESSION(pMac, sessionId);
   tpRrmSMEContext          pSmeRrmContext   = &pMac->rrm.rrmSmeContext;

   if (pSmeRrmContext->eseBcnReqInProgress == TRUE)
   {
      smsLog(pMac, LOGE, "A Beacon Report Req is already in progress");
      return eHAL_STATUS_RESOURCES;
   }

   /* Store the info in RRM context */
   vos_mem_copy(&pSmeRrmContext->eseBcnReqInfo, pEseBcnReq, sizeof(tCsrEseBeaconReq));

   //Prepare the request to send to SME.
   pSmeBcnReportReq = vos_mem_malloc(sizeof( tSirBeaconReportReqInd ));
   if(NULL == pSmeBcnReportReq)
   {
      smsLog(pMac, LOGP, "Memory Allocation Failure!!! ESE  BcnReq Ind to SME");
      return eSIR_FAILURE;
   }

   pSmeRrmContext->eseBcnReqInProgress = TRUE;

   smsLog(pMac, LOGE, "Sending Beacon Report Req to SME");
   vos_mem_zero( pSmeBcnReportReq, sizeof( tSirBeaconReportReqInd ));

   pSmeBcnReportReq->messageType = eWNI_SME_BEACON_REPORT_REQ_IND;
   pSmeBcnReportReq->length = sizeof( tSirBeaconReportReqInd );
   vos_mem_copy( pSmeBcnReportReq->bssId, pSession->connectedProfile.bssid, sizeof(tSirMacAddr) );
   pSmeBcnReportReq->channelInfo.channelNum = 255;
   pSmeBcnReportReq->channelList.numChannels = pEseBcnReq->numBcnReqIe;
   pSmeBcnReportReq->msgSource = eRRM_MSG_SOURCE_ESE_UPLOAD;

   for (counter = 0; counter < pEseBcnReq->numBcnReqIe; counter++)
   {
        pBeaconReq = (tCsrEseBeaconReqParams *)&pEseBcnReq->bcnReq[counter];
        pSmeBcnReportReq->fMeasurementtype[counter] = pBeaconReq->scanMode;
        pSmeBcnReportReq->measurementDuration[counter] = SYS_TU_TO_MS(pBeaconReq->measurementDuration);
        pSmeBcnReportReq->channelList.channelNumber[counter] = pBeaconReq->channel;
   }

   status = sme_RrmProcessBeaconReportReqInd(pMac, pSmeBcnReportReq);

   if(status != eHAL_STATUS_SUCCESS)
      pSmeRrmContext->eseBcnReqInProgress = FALSE;

   return status;
}

#endif /* FEATURE_WLAN_ESE && FEATURE_WLAN_ESE_UPLOAD */

/**
 * sme_process_fw_mem_dump_rsp - process fw memory dump response from WMA
 *
 * @pMac - pointer to MAC handle.
 * @pMsg - pointer to received SME msg.
 *
 * This function process the received SME message and calls the corresponding
 * callback which was already registered with SME.
 */
#ifdef WLAN_FEATURE_MEMDUMP
static void sme_process_fw_mem_dump_rsp(tpAniSirGlobal pMac, vos_msg_t* pMsg)
{
	if (pMsg->bodyptr) {
		if (pMac->sme.fw_dump_callback)
			pMac->sme.fw_dump_callback(pMac->hHdd,
				(struct fw_dump_rsp*) pMsg->bodyptr);
		vos_mem_free(pMsg->bodyptr);
	}
}
#else
static void sme_process_fw_mem_dump_rsp(tpAniSirGlobal pMac, vos_msg_t* pMsg)
{
}
#endif
eHalStatus sme_IbssPeerInfoResponseHandleer( tHalHandle hHal,
                                      tpSirIbssGetPeerInfoRspParams pIbssPeerInfoParams)
{
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if (NULL == pMac)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_FATAL,
           "%s: pMac is null", __func__);
       return eHAL_STATUS_FAILURE;
   }
   if (pMac->sme.peerInfoParams.peerInfoCbk == NULL)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
           "%s: HDD callback is null", __func__);
       return eHAL_STATUS_FAILURE;
   }
   pMac->sme.peerInfoParams.peerInfoCbk(pMac->sme.peerInfoParams.pUserData,
                                        &pIbssPeerInfoParams->ibssPeerInfoRspParams);
   return eHAL_STATUS_SUCCESS;
}

/*--------------------------------------------------------------------------

  \brief sme_ProcessMsg() - The main message processor for SME.

  The function is called by a message dispatcher when to process a message
  targeted for SME.

  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param pMsg - A pointer to a caller allocated object of tSirMsgQ.

  \return eHAL_STATUS_SUCCESS - SME successfully process the message.

          Other status means SME failed to process the message to HAL.
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_ProcessMsg(tHalHandle hHal, vos_msg_t* pMsg)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if (pMsg == NULL) {
      smsLog( pMac, LOGE, "Empty message for SME, nothing to process");
      return status;
   }

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( SME_IS_START(pMac) )
      {
          switch (pMsg->type) { // TODO: Will be modified to do a range check for msgs instead of having cases for each msgs
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
          case eWNI_SME_HO_FAIL_IND:
               VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "LFR3:%s: Rcvd eWNI_SME_HO_FAIL_IND", __func__);
               csrProcessHOFailInd(pMac, pMsg->bodyptr);
               vos_mem_free(pMsg->bodyptr);
               break;
	  case eWNI_SME_ROAM_OFFLOAD_SYNCH_IND:
	       csrProcessRoamOffloadSynchInd(pMac,
			       (tpSirRoamOffloadSynchInd)pMsg->bodyptr);
               vos_mem_free(pMsg->bodyptr);
	       break;
#endif
          case eWNI_PMC_ENTER_BMPS_RSP:
          case eWNI_PMC_EXIT_BMPS_RSP:
          case eWNI_PMC_EXIT_BMPS_IND:
          case eWNI_PMC_ENTER_IMPS_RSP:
          case eWNI_PMC_EXIT_IMPS_RSP:
          case eWNI_PMC_SMPS_STATE_IND:
          case eWNI_PMC_ENTER_UAPSD_RSP:
          case eWNI_PMC_EXIT_UAPSD_RSP:
          case eWNI_PMC_ENTER_WOWL_RSP:
          case eWNI_PMC_EXIT_WOWL_RSP:
             //PMC
             if (pMsg->bodyptr)
             {
                if(!pMac->psOffloadEnabled)
                {
                    pmcMessageProcessor(hHal, pMsg->bodyptr);
                }
                else
                {
                    pmcOffloadMessageProcessor(hHal, pMsg->bodyptr);
                }
                status = eHAL_STATUS_SUCCESS;
                vos_mem_free(pMsg->bodyptr);
             } else {
                smsLog( pMac, LOGE, "Empty rsp message for PMC, nothing to process");
             }
             break;

          case WNI_CFG_SET_CNF:
          case WNI_CFG_DNLD_CNF:
          case WNI_CFG_GET_RSP:
          case WNI_CFG_ADD_GRP_ADDR_CNF:
          case WNI_CFG_DEL_GRP_ADDR_CNF:
             //CCM
             if (pMsg->bodyptr)
             {
                ccmCfgCnfMsgHandler(hHal, pMsg->bodyptr);
                status = eHAL_STATUS_SUCCESS;
                vos_mem_free(pMsg->bodyptr);
             } else {
                smsLog( pMac, LOGE, "Empty rsp message for CCM, nothing to process");
             }
             break;

          case eWNI_SME_ADDTS_RSP:
          case eWNI_SME_DELTS_RSP:
          case eWNI_SME_DELTS_IND:
#ifdef WLAN_FEATURE_VOWIFI_11R
          case eWNI_SME_FT_AGGR_QOS_RSP:
#endif
             //QoS
             if (pMsg->bodyptr)
             {
#ifndef WLAN_MDM_CODE_REDUCTION_OPT
                status = sme_QosMsgProcessor(pMac, pMsg->type, pMsg->bodyptr);
                vos_mem_free(pMsg->bodyptr);
#endif
             } else {
                smsLog( pMac, LOGE, "Empty rsp message for QoS, nothing to process");
             }
             break;
#if defined WLAN_FEATURE_VOWIFI
          case eWNI_SME_NEIGHBOR_REPORT_IND:
          case eWNI_SME_BEACON_REPORT_REQ_IND:
#if defined WLAN_VOWIFI_DEBUG
             smsLog( pMac, LOGE, "Received RRM message. Message Id = %d", pMsg->type );
#endif
             if ( pMsg->bodyptr )
             {
                status = sme_RrmMsgProcessor( pMac, pMsg->type, pMsg->bodyptr );
                vos_mem_free(pMsg->bodyptr);
             }
             else
             {
                smsLog( pMac, LOGE, "Empty message for RRM, nothing to process");
             }
             break;
#endif

#ifdef FEATURE_OEM_DATA_SUPPORT
          //Handle the eWNI_SME_OEM_DATA_RSP:
          case eWNI_SME_OEM_DATA_RSP:
                if(pMsg->bodyptr)
                {
                        status = sme_HandleOemDataRsp(pMac, pMsg->bodyptr);
                        vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                        smsLog( pMac, LOGE, "Empty rsp message for oemData_ (eWNI_SME_OEM_DATA_RSP), nothing to process");
                }
                smeProcessPendingQueue( pMac );
                break;
#endif

          case eWNI_SME_ADD_STA_SELF_RSP:
                if(pMsg->bodyptr)
                {
                   status = csrProcessAddStaSessionRsp(pMac, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog( pMac, LOGE, "Empty rsp message for meas (eWNI_SME_ADD_STA_SELF_RSP), nothing to process");
                }
                break;
          case eWNI_SME_DEL_STA_SELF_RSP:
                if(pMsg->bodyptr)
                {
                   status = csrProcessDelStaSessionRsp(pMac, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog( pMac, LOGE, "Empty rsp message for meas (eWNI_SME_DEL_STA_SELF_RSP), nothing to process");
                }
                break;
          case eWNI_SME_REMAIN_ON_CHN_RSP:
                if(pMsg->bodyptr)
                {
                    status = sme_remainOnChnRsp(pMac, pMsg->bodyptr);
                    vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                    smsLog( pMac, LOGE, "Empty rsp message for meas (eWNI_SME_REMAIN_ON_CHN_RSP), nothing to process");
                }
                break;
          case eWNI_SME_REMAIN_ON_CHN_RDY_IND:
                if(pMsg->bodyptr)
                {
                    status = sme_remainOnChnReady(pMac, pMsg->bodyptr);
                    vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                    smsLog( pMac, LOGE, "Empty rsp message for meas (eWNI_SME_REMAIN_ON_CHN_RDY_IND), nothing to process");
                }
                break;
           case eWNI_SME_ACTION_FRAME_SEND_CNF:
                if(pMsg->bodyptr)
                {
                    status = sme_sendActionCnf(pMac, pMsg->bodyptr);
                    vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                    smsLog( pMac, LOGE, "Empty rsp message for meas (eWNI_SME_ACTION_FRAME_SEND_CNF), nothing to process");
                }
                break;
          case eWNI_SME_COEX_IND:
                MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                              NO_SESSION, pMsg->type));
                if(pMsg->bodyptr)
                {
                   status = btcHandleCoexInd((void *)pMac, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog(pMac, LOGE, "Empty rsp message for meas (eWNI_SME_COEX_IND), nothing to process");
                }
                break;

#ifdef FEATURE_WLAN_SCAN_PNO
          case eWNI_SME_PREF_NETWORK_FOUND_IND:
                MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                              NO_SESSION, pMsg->type));
                if(pMsg->bodyptr)
                {
                   status = sme_PreferredNetworkFoundInd((void *)pMac, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog(pMac, LOGE, "Empty rsp message for meas (eWNI_SME_PREF_NETWORK_FOUND_IND), nothing to process");
                }
                break;
#endif // FEATURE_WLAN_SCAN_PNO

          case eWNI_SME_TX_PER_HIT_IND:
                MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                              NO_SESSION, pMsg->type));
                if (pMac->sme.pTxPerHitCallback)
                {
                   pMac->sme.pTxPerHitCallback(pMac->sme.pTxPerHitCbContext);
                }
                break;

          case eWNI_SME_CHANGE_COUNTRY_CODE:
              if(pMsg->bodyptr)
                {
                   status = sme_HandleChangeCountryCode((void *)pMac, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                    smsLog(pMac, LOGE, "Empty rsp message for message (eWNI_SME_CHANGE_COUNTRY_CODE), nothing to process");
                }
                break;

          case eWNI_SME_GENERIC_CHANGE_COUNTRY_CODE:
              if (pMsg->bodyptr)
                {
                    status = sme_HandleGenericChangeCountryCode((void *)pMac, pMsg->bodyptr);
                    vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog(pMac, LOGE, "Empty rsp message for message (eWNI_SME_GENERIC_CHANGE_COUNTRY_CODE), nothing to process");
                }
                break;

#ifdef WLAN_FEATURE_PACKET_FILTERING
          case eWNI_PMC_PACKET_COALESCING_FILTER_MATCH_COUNT_RSP:
                MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                              NO_SESSION, pMsg->type));
                if(pMsg->bodyptr)
                {
                   status = sme_PCFilterMatchCountResponseHandler((void *)pMac, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog(pMac, LOGE, "Empty rsp message for meas "
                          "(PACKET_COALESCING_FILTER_MATCH_COUNT_RSP), nothing to process");
                }
                break;
#endif // WLAN_FEATURE_PACKET_FILTERING
          case eWNI_SME_PRE_SWITCH_CHL_IND:
                if(pMsg->bodyptr)
                {
                   status = sme_HandlePreChannelSwitchInd(pMac,pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog(pMac, LOGE, "Empty rsp message for meas "
                          "(eWNI_SME_PRE_SWITCH_CHL_IND), nothing to process");
                }
                break;
          case eWNI_SME_POST_SWITCH_CHL_IND:
             {
                status = sme_HandlePostChannelSwitchInd(pMac);
                break;
             }

#ifdef WLAN_WAKEUP_EVENTS
          case eWNI_SME_WAKE_REASON_IND:
                MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                              NO_SESSION, pMsg->type));
                if(pMsg->bodyptr)
                {
                   status = sme_WakeReasonIndCallback((void *)pMac, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog(pMac, LOGE, "Empty rsp message for meas (eWNI_SME_WAKE_REASON_IND), nothing to process");
                }
                break;
#endif // WLAN_WAKEUP_EVENTS

#ifdef FEATURE_WLAN_TDLS
          /*
           * command received from PE, SME tdls msg processor shall be called
           * to process commands received from PE
           */
          case eWNI_SME_TDLS_SEND_MGMT_RSP:
          case eWNI_SME_TDLS_ADD_STA_RSP:
          case eWNI_SME_TDLS_DEL_STA_RSP:
          case eWNI_SME_TDLS_DEL_STA_IND:
          case eWNI_SME_TDLS_DEL_ALL_PEER_IND:
          case eWNI_SME_MGMT_FRM_TX_COMPLETION_IND:
          case eWNI_SME_TDLS_LINK_ESTABLISH_RSP:
          case eWNI_SME_TDLS_SHOULD_DISCOVER:
          case eWNI_SME_TDLS_SHOULD_TEARDOWN:
          case eWNI_SME_TDLS_PEER_DISCONNECTED:
                {
                    if (pMsg->bodyptr)
                    {
                        status = tdlsMsgProcessor(pMac, pMsg->type, pMsg->bodyptr);
                        vos_mem_free(pMsg->bodyptr);
                    }
                    else
                    {
                        smsLog( pMac, LOGE,
                                "Empty rsp message for TDLS, nothing to process");
                    }
                    break;
                }
#endif

#ifdef WLAN_FEATURE_11W
           case eWNI_SME_UNPROT_MGMT_FRM_IND:
                if (pMsg->bodyptr)
                {
                    sme_UnprotectedMgmtFrmInd(pMac, pMsg->bodyptr);
                    vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                    smsLog(pMac, LOGE, "Empty rsp message for meas (eWNI_SME_UNPROT_MGMT_FRM_IND), nothing to process");
                }
                break;
#endif
#if defined(FEATURE_WLAN_ESE) && defined(FEATURE_WLAN_ESE_UPLOAD)
       case eWNI_SME_TSM_IE_IND:
       {
        if (pMsg->bodyptr)
        {
            sme_TsmIeInd(pMac, pMsg->bodyptr);
            vos_mem_free(pMsg->bodyptr);
        }
        else
        {
            smsLog(pMac, LOGE,
            "Empty rsp message for (eWNI_SME_TSM_IE_IND), nothing to process");
        }
        break;
       }
#endif /* FEATURE_WLAN_ESE && FEATURE_WLAN_ESE_UPLOAD */
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
          case eWNI_SME_ROAM_SCAN_OFFLOAD_RSP:
                status = csrRoamOffloadScanRspHdlr((void *)pMac, pMsg->bodyptr);
                vos_mem_free(pMsg->bodyptr);
                break;
#endif // WLAN_FEATURE_ROAM_SCAN_OFFLOAD

#ifdef WLAN_FEATURE_GTK_OFFLOAD
           case eWNI_PMC_GTK_OFFLOAD_GETINFO_RSP:
                MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                               NO_SESSION, pMsg->type));
                if (pMsg->bodyptr)
                {
                    sme_ProcessGetGtkInfoRsp(pMac, pMsg->bodyptr);
                    vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                    smsLog(pMac, LOGE, "Empty rsp message for (eWNI_PMC_GTK_OFFLOAD_GETINFO_RSP), nothing to process");
                }
                break ;
#endif

#ifdef FEATURE_WLAN_LPHB
          /* LPHB timeout indication arrived, send IND to client */
          case eWNI_SME_LPHB_IND:
                MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                              NO_SESSION, pMsg->type));
                if (pMac->sme.pLphbIndCb)
                {
                   pMac->sme.pLphbIndCb(pMac->hHdd, pMsg->bodyptr);
                }
                vos_mem_free(pMsg->bodyptr);

                break;
#endif /* FEATURE_WLAN_LPHB */

          case eWNI_SME_IBSS_PEER_INFO_RSP:
              MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                            NO_SESSION, pMsg->type));
              if (pMsg->bodyptr)
              {
                  sme_IbssPeerInfoResponseHandleer(pMac, pMsg->bodyptr);
                  vos_mem_free(pMsg->bodyptr);
              }
              else
              {
                  smsLog(pMac, LOGE,
                         "Empty rsp message for (eWNI_SME_IBSS_PEER_INFO_RSP),"
                         " nothing to process");
              }
              break ;

           case eWNI_SME_READY_TO_SUSPEND_IND:
                if (pMsg->bodyptr)
                {
                    sme_ProcessReadyToSuspend(pMac, pMsg->bodyptr);
                    vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                    smsLog(pMac, LOGE, "Empty rsp message for (eWNI_SME_READY_TO_SUSPEND_IND), nothing to process");
                }
                break ;

#ifdef WLAN_FEATURE_EXTWOW_SUPPORT
           case eWNI_SME_READY_TO_EXTWOW_IND:
                if (pMsg->bodyptr)
                {
                     sme_ProcessReadyToExtWoW(pMac, pMsg->bodyptr);
                     vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                     smsLog(pMac, LOGE, "Empty rsp message"
                     "for (eWNI_SME_READY_TO_EXTWOW_IND), nothing to process");
                }
                break ;
#endif

#ifdef FEATURE_WLAN_CH_AVOID
           /* channel avoid message arrived, send IND to client */
           case eWNI_SME_CH_AVOID_IND:
                MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                              NO_SESSION, pMsg->type));
                if (pMac->sme.pChAvoidNotificationCb)
                {
                   VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                             "%s: CH avoid notification", __func__);
                   pMac->sme.pChAvoidNotificationCb(pMac->hHdd, pMsg->bodyptr);
                }
                vos_mem_free(pMsg->bodyptr);
                break;
#endif /* FEATURE_WLAN_CH_AVOID */

#ifdef FEATURE_WLAN_AUTO_SHUTDOWN
           case eWNI_SME_AUTO_SHUTDOWN_IND:
                if (pMac->sme.pAutoShutdownNotificationCb)
                {
                   VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                             "%s: Auto shutdown notification", __func__);
                   pMac->sme.pAutoShutdownNotificationCb();
                }
                vos_mem_free(pMsg->bodyptr);
                break;
#endif
           case eWNI_SME_DFS_RADAR_FOUND:
           case eWNI_SME_DFS_CSAIE_TX_COMPLETE_IND:
                {
                    status = dfsMsgProcessor(pMac, pMsg->type, pMsg->bodyptr);
                    vos_mem_free( pMsg->bodyptr );
                }
                break;
           case eWNI_SME_EXT_CHANGE_CHANNEL_IND:
                status = sme_extended_change_channel_ind(pMac, pMsg->bodyptr);
                vos_mem_free(pMsg->bodyptr);
                break;
           case eWNI_SME_CHANNEL_CHANGE_RSP:
                if (pMsg->bodyptr)
                {
                    status = sme_ProcessChannelChangeResp(pMac,
                                           pMsg->type, pMsg->bodyptr);
                    vos_mem_free( pMsg->bodyptr );
                }
                else
                {
                    smsLog( pMac, LOGE,
                            "Empty rsp message for (eWNI_SME_CHANNEL_CHANGE_RSP),"
                            "nothing to process");
                }
                break ;

#ifdef WLAN_FEATURE_STATS_EXT
          case eWNI_SME_STATS_EXT_EVENT:
              if (pMsg->bodyptr)
              {
                  status = sme_StatsExtEvent(hHal, pMsg->bodyptr);
                  vos_mem_free(pMsg->bodyptr);
              }
              else
              {
                  smsLog( pMac, LOGE,
                          "Empty event message for eWNI_SME_STATS_EXT_EVENT, nothing to process");
              }
              break;
#endif
          case eWNI_SME_LINK_SPEED_IND:
               if (pMac->sme.pLinkSpeedIndCb)
               {
                   pMac->sme.pLinkSpeedIndCb(pMsg->bodyptr,
                                             pMac->sme.pLinkSpeedCbContext);
               }
               if (pMsg->bodyptr)
               {
                   vos_mem_free(pMsg->bodyptr);
               }
               break;
          case eWNI_SME_GET_RSSI_IND:
               if (pMac->sme.pget_rssi_ind_cb)
                   pMac->sme.pget_rssi_ind_cb(pMsg->bodyptr,
                                            pMac->sme.pget_rssi_cb_context);
               vos_mem_free(pMsg->bodyptr);
               break;
          case eWNI_SME_CSA_OFFLOAD_EVENT:
               if (pMsg->bodyptr)
               {
                   csrScanFlushBssEntry(pMac, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
               }
               break;
          case eWNI_SME_TSF_EVENT:
               if (pMac->sme.get_tsf_cb) {
                   pMac->sme.get_tsf_cb(pMac->sme.get_tsf_cxt,
                                      (struct stsf *)pMsg->bodyptr);
               }
               if (pMsg->bodyptr) {
                   vos_mem_free(pMsg->bodyptr);
                   pMsg->bodyptr = NULL;
               }
               break;
#ifdef WLAN_FEATURE_NAN
          case eWNI_SME_NAN_EVENT:
              MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_WDA_MSG,
                                            NO_SESSION, pMsg->type));
                if (pMsg->bodyptr)
                {
                    sme_NanEvent(hHal, pMsg->bodyptr);
                    vos_mem_free(pMsg->bodyptr);
                }
                break;
#endif /* WLAN_FEATURE_NAN */
          case eWNI_SME_LINK_STATUS_IND:
          {
                tAniGetLinkStatus *pLinkStatus =
                             (tAniGetLinkStatus *) pMsg->bodyptr;
                if (pLinkStatus) {
                    if (pMac->sme.linkStatusCallback) {
                        pMac->sme.linkStatusCallback(pLinkStatus->linkStatus,
                                               pMac->sme.linkStatusContext);
                    }

                    pMac->sme.linkStatusCallback = NULL;
                    pMac->sme.linkStatusContext = NULL;
                    vos_mem_free(pLinkStatus);
                }
                break;
          }
          case eWNI_SME_MSG_GET_TEMPERATURE_IND:
               if (pMac->sme.pGetTemperatureCb)
               {
                   pMac->sme.pGetTemperatureCb(pMsg->bodyval,
                           pMac->sme.pTemperatureCbContext);
               }
               break;
          case eWNI_SME_SNR_IND:
          {
                tAniGetSnrReq *pSnrReq = (tAniGetSnrReq *) pMsg->bodyptr;
                if (pSnrReq) {
                    if (pSnrReq->snrCallback) {
                        ((tCsrSnrCallback)(pSnrReq->snrCallback))(pSnrReq->snr,
                                                    pSnrReq->staId,
                                                    pSnrReq->pDevContext);
                    }

                    vos_mem_free(pSnrReq);
                }
                break;
          }
#ifdef FEATURE_WLAN_EXTSCAN
          case eWNI_SME_EXTSCAN_FULL_SCAN_RESULT_IND:
          {
		if (pMac->sme.pExtScanIndCb) {
                    pMac->sme.pExtScanIndCb(pMac->hHdd,
                                            eSIR_EXTSCAN_FULL_SCAN_RESULT_IND,
                                            pMsg->bodyptr);
                } else {
                    smsLog(pMac, LOGE,
                           FL("callback not registered to process eWNI_SME_EXTSCAN_FULL_SCAN_RESULT_IND"));
                }
                vos_mem_free(pMsg->bodyptr);
                break;
          }
          case eWNI_SME_EPNO_NETWORK_FOUND_IND:
          {
                if (pMac->sme.pExtScanIndCb) {
                    pMac->sme.pExtScanIndCb(pMac->hHdd,
                                            eSIR_EPNO_NETWORK_FOUND_IND,
                                            pMsg->bodyptr);
                } else {
                    smsLog(pMac, LOGE,
                           FL("callback not registered to process eWNI_SME_EPNO_NETWORK_FOUND_IND"));
                }
                vos_mem_free(pMsg->bodyptr);
                break;
          }
#endif
          case eWNI_SME_FW_STATUS_IND:
               if (pMac->sme.fw_state_callback)
                   pMac->sme.fw_state_callback(pMac->sme.fw_state_context);

               pMac->sme.fw_state_callback = NULL;
               pMac->sme.fw_state_context = NULL;
               break;
          case eWNI_SME_OCB_SET_CONFIG_RSP:
               if (pMac->sme.ocb_set_config_callback) {
                   pMac->sme.ocb_set_config_callback(
                       pMac->sme.ocb_set_config_context,
                       pMsg->bodyptr);
               } else {
                   smsLog(pMac, LOGE, FL(
                       "Error processing message. The callback is NULL."));
               }
               pMac->sme.ocb_set_config_callback = NULL;
               pMac->sme.ocb_set_config_context = NULL;
               vos_mem_free(pMsg->bodyptr);
               break;
          case eWNI_SME_OCB_GET_TSF_TIMER_RSP:
               if (pMac->sme.ocb_get_tsf_timer_callback) {
                   pMac->sme.ocb_get_tsf_timer_callback(
                       pMac->sme.ocb_get_tsf_timer_context,
                       pMsg->bodyptr);
               } else {
                   smsLog(pMac, LOGE, FL(
                       "Error processing message. The callback is NULL."));
               }
               pMac->sme.ocb_get_tsf_timer_callback = NULL;
               pMac->sme.ocb_get_tsf_timer_context = NULL;
               vos_mem_free(pMsg->bodyptr);
               break;
          case eWNI_SME_DCC_GET_STATS_RSP:
               if (pMac->sme.dcc_get_stats_callback) {
                   pMac->sme.dcc_get_stats_callback(
                       pMac->sme.dcc_get_stats_context,
                       pMsg->bodyptr);
               } else {
                   smsLog(pMac, LOGE, FL(
                       "Error processing message. The callback or context is NULL."));
               }
               pMac->sme.dcc_get_stats_callback = NULL;
               pMac->sme.dcc_get_stats_context = NULL;
               vos_mem_free(pMsg->bodyptr);
               break;
          case eWNI_SME_DCC_UPDATE_NDL_RSP:
               if (pMac->sme.dcc_update_ndl_callback) {
                   pMac->sme.dcc_update_ndl_callback(
                       pMac->sme.dcc_update_ndl_context,
                       pMsg->bodyptr);
               } else {
                   smsLog(pMac, LOGE, FL(
                       "Error processing message. The callback or context is NULL."));
               }
               pMac->sme.dcc_update_ndl_callback = NULL;
               pMac->sme.dcc_update_ndl_context = NULL;
               vos_mem_free(pMsg->bodyptr);
               break;
          case eWNI_SME_DCC_STATS_EVENT:
               if (pMac->sme.dcc_stats_event_callback) {
                   pMac->sme.dcc_stats_event_callback(
                       pMac->sme.dcc_stats_event_context,
                       pMsg->bodyptr);
               } else {
                   smsLog(pMac, LOGE, FL(
                       "Error processing message. The callback or context is NULL."));
               }
               vos_mem_free(pMsg->bodyptr);
               break;
          case eWNI_SME_FW_DUMP_IND:
               sme_process_fw_mem_dump_rsp(pMac, pMsg);
               break;
          case eWNI_SME_SET_THERMAL_LEVEL_IND:
               if (pMac->sme.set_thermal_level_cb)
               {
                   pMac->sme.set_thermal_level_cb(pMac->hHdd, pMsg->bodyval);
               }
               break;
          case eWNI_SME_LOST_LINK_INFO_IND:
               if (pMac->sme.lost_link_info_cb) {
                   pMac->sme.lost_link_info_cb(pMac->hHdd,
                             (struct sir_lost_link_info *)pMsg->bodyptr);
               }
               vos_mem_free(pMsg->bodyptr);
               break;
          case eWNI_SME_SMPS_FORCE_MODE_IND:
               if (pMac->sme.smps_force_mode_cb)
                   pMac->sme.smps_force_mode_cb(pMac->hHdd,
                       (struct sir_smps_force_mode_event *)
                       pMsg->bodyptr);
               vos_mem_free(pMsg->bodyptr);
               break;
          case eWNI_SME_NDP_CONFIRM_IND:
          case eWNI_SME_NDP_NEW_PEER_IND:
          case eWNI_SME_NDP_INITIATOR_RSP:
          case eWNI_SME_NDP_INDICATION:
          case eWNI_SME_NDP_RESPONDER_RSP:
               sme_ndp_msg_processor(pMac, pMsg);
               break;
          default:

             if ( ( pMsg->type >= eWNI_SME_MSG_TYPES_BEGIN )
                  &&  ( pMsg->type <= eWNI_SME_MSG_TYPES_END ) )
             {
                //CSR
                if (pMsg->bodyptr)
                {
                   status = csrMsgProcessor(hHal, pMsg->bodyptr);
                   vos_mem_free(pMsg->bodyptr);
                }
                else
                {
                   smsLog( pMac, LOGE, "Empty rsp message for CSR, nothing to process");
                }
             }
             else
             {
                smsLog( pMac, LOGW, "Unknown message type %d, nothing to process",
                        pMsg->type);
                if (pMsg->bodyptr)
                {
                   vos_mem_free(pMsg->bodyptr);
                }
             }
          }//switch
      } //SME_IS_START
      else
      {
         smsLog( pMac, LOGW, "message type %d in stop state ignored", pMsg->type);
         if (pMsg->bodyptr)
         {
            vos_mem_free(pMsg->bodyptr);
         }
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }
   else
   {
      smsLog( pMac, LOGW, "Locking failed, bailing out");
      if (pMsg->bodyptr)
      {
          vos_mem_free(pMsg->bodyptr);
      }
   }

   return status;
}


//No need to hold the global lock here because this function can only be called
//after sme_Stop.
v_VOID_t sme_FreeMsg( tHalHandle hHal, vos_msg_t* pMsg )
{
   if( pMsg )
   {
      if (pMsg->bodyptr)
      {
         vos_mem_free(pMsg->bodyptr);
      }
   }

}


/*--------------------------------------------------------------------------

  \brief sme_Stop() - Stop all SME modules and put them at idle state

  The function stops each module in SME, PMC, CCM, CSR, etc. . Upon
  return, all modules are at idle state ready to start.

  This is a synchronous call
  \param hHal - The handle returned by macOpen
  \param tHalStopType - reason for stopping

  \return eHAL_STATUS_SUCCESS - SME is stopped.

          Other status means SME is failed to stop but caller should still
          consider SME is stopped.
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_Stop(tHalHandle hHal, tHalStopType stopType)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   eHalStatus fail_status = eHAL_STATUS_SUCCESS;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

#ifndef WLAN_FEATURE_MBSSID
   status = WLANSAP_Stop(vos_get_global_context(VOS_MODULE_ID_SAP, NULL));
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "WLANSAP_Stop failed during smeStop with status=%d",
                          status );
      fail_status = status;
   }
#endif

   p2pStop(hHal);

   if(!pMac->psOffloadEnabled)
   {
       status = pmcStop(hHal);
       if ( ! HAL_STATUS_SUCCESS( status ) ) {
           smsLog( pMac, LOGE,
                   "pmcStop failed during smeStop with status=%d",
                   status );
           fail_status = status;
       }
   }
   else
   {
       status = pmcOffloadStop(hHal);
       if ( ! HAL_STATUS_SUCCESS( status ) ) {
           smsLog( pMac, LOGE,
                   "pmcOffloadStop failed during smeStop with status=%d",
                   status );
           fail_status = status;
       }
   }

   status = csrStop(pMac, stopType);
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "csrStop failed during smeStop with status=%d",
              status );
      fail_status = status;
   }

   ccmStop(hHal);

   purgeSmeCmdList(pMac);

   if (!HAL_STATUS_SUCCESS( fail_status )) {
      status = fail_status;
   }

   pMac->sme.state = SME_STATE_STOP;

   return status;
}

/*--------------------------------------------------------------------------

  \brief sme_Close() - Release all SME modules and their resources.

  The function release each module in SME, PMC, CCM, CSR, etc. . Upon
  return, all modules are at closed state.

  No SME APIs can be involved after smeClose except smeOpen.
  smeClose must be called before macClose.
  This is a synchronous call
  \param hHal - The handle returned by macOpen

  \return eHAL_STATUS_SUCCESS - SME is successfully close.

          Other status means SME is failed to be closed but caller still cannot
          call any other SME functions except smeOpen.
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_Close(tHalHandle hHal)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   eHalStatus fail_status = eHAL_STATUS_SUCCESS;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if (!pMac)
       return eHAL_STATUS_FAILURE;

   /* Note: pSession will be invalid from here on, do not access */
   status = csrClose(pMac);
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "csrClose failed during sme close with status=%d",
              status );
      fail_status = status;
   }

#ifndef WLAN_FEATURE_MBSSID
   status = WLANSAP_Close(vos_get_global_context(VOS_MODULE_ID_SAP, NULL));
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "WLANSAP_close failed during sme close with status=%d",
              status );
      fail_status = status;
   }
#endif

#ifndef WLAN_MDM_CODE_REDUCTION_OPT
   status = btcClose(hHal);
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "BTC close failed during sme close with status=%d",
              status );
      fail_status = status;
   }

   status = sme_QosClose(pMac);
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "Qos close failed during sme close with status=%d",
              status );
      fail_status = status;
   }
#endif
#ifdef FEATURE_OEM_DATA_SUPPORT
   status = oemData_OemDataReqClose(hHal);
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
       smsLog( pMac, LOGE, "OEM DATA REQ close failed during sme close with status=%d",
              status );
      fail_status = status;
   }
#endif

   status = ccmClose(hHal);
         if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "ccmClose failed during sme close with status=%d",
                 status );
             fail_status = status;
         }

   if(!pMac->psOffloadEnabled)
   {
       status = pmcClose(hHal);
       if ( ! HAL_STATUS_SUCCESS( status ) ) {
          smsLog(pMac, LOGE, "pmcClose failed during sme close with status=%d",
              status);
          fail_status = status;
       }
   }
   else
   {
       status = pmcOffloadClose(hHal);
       if(!HAL_STATUS_SUCCESS(status)) {
          smsLog(pMac, LOGE, "pmcOffloadClose failed during smeClose status=%d",
              status);
          fail_status = status;
       }
   }
#if defined WLAN_FEATURE_VOWIFI
   status = rrmClose(hHal);
   if ( ! HAL_STATUS_SUCCESS( status ) ) {
      smsLog( pMac, LOGE, "RRM close failed during sme close with status=%d",
              status );
      fail_status = status;
   }
#endif

   sme_p2pClose(hHal);

   freeSmeCmdList(pMac);

   if( !VOS_IS_STATUS_SUCCESS( vos_lock_destroy( &pMac->sme.lkSmeGlobalLock ) ) )
   {
       fail_status = eHAL_STATUS_FAILURE;
   }

   if (!HAL_STATUS_SUCCESS( fail_status )) {
      status = fail_status;
   }

   pMac->sme.state = SME_STATE_STOP;

   return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_ScanRequest
    \brief a wrapper function to Request a 11d or full scan from CSR.
    This is an asynchronous call
    \param pScanRequestID - pointer to an object to get back the request ID
    \param callback - a callback function that scan calls upon finish, will not
                      be called if csrScanRequest returns error
    \param pContext - a pointer passed in for the callback
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_ScanRequest(tHalHandle hHal, tANI_U8 sessionId, tCsrScanRequest *pscanReq,
                           tANI_U32 *pScanRequestID,
                           csrScanCompleteCallback callback, void *pContext)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_MSG_SCAN_REQ, sessionId, pscanReq->scanType));
    smsLog(pMac, LOG2, FL("enter"));
    do
    {
        if(pMac->scan.fScanEnable)
        {
            status = sme_AcquireGlobalLock( &pMac->sme );
            if (HAL_STATUS_SUCCESS(status )) {
                status = csrScanRequest(hHal, sessionId, pscanReq,
                                        pScanRequestID, callback, pContext);
                if (!HAL_STATUS_SUCCESS(status)) {
                   smsLog(pMac, LOGE,
                          FL("csrScanRequest failed sessionId(%d)"), sessionId);
                }
                sme_ReleaseGlobalLock( &pMac->sme );
            } else {
                smsLog(pMac, LOGE, FL("sme_AcquireGlobalLock failed"));
            }
        } //if(pMac->scan.fScanEnable)
        else
        {
            smsLog(pMac, LOGE, FL("fScanEnable FALSE"));
        }
    } while( 0 );

    return (status);


}

/* ---------------------------------------------------------------------------
    \fn sme_ScanGetResult
    \brief a wrapper function to request scan results from CSR.
    This is a synchronous call
    \param pFilter - If pFilter is NULL, all cached results are returned
    \param phResult - an object for the result.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_ScanGetResult(tHalHandle hHal, tANI_U8 sessionId, tCsrScanResultFilter *pFilter,
                            tScanResultHandle *phResult)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_MSG_SCAN_GET_RESULTS, sessionId,0 ));
   smsLog(pMac, LOG2, FL("enter"));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = csrScanGetResult( hHal, pFilter, phResult );
       sme_ReleaseGlobalLock( &pMac->sme );
   }
   smsLog(pMac, LOG2, FL("exit status %d"), status);

   return (status);
}

/**
 * sme_get_ap_channel_from_scan_cache() - a wrapper function to get AP's
 *                                        channel id from CSR by filtering the
 *                                        result which matches our roam profile.
 * @profile: SAP adapter
 * @ap_chnl_id: pointer to channel id of SAP. Fill the value after finding the
 *              best ap from scan cache.
 *
 * This function is written to get AP's channel id from CSR by filtering
 * the result which matches our roam profile. This is a synchronous call.
 *
 * Return: VOS_STATUS.
 */
VOS_STATUS sme_get_ap_channel_from_scan_cache(tHalHandle hHal,
                                              tCsrRoamProfile *profile,
                                              tScanResultHandle *scan_cache,
                                              tANI_U8 *ap_chnl_id)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tCsrScanResultFilter *scan_filter = NULL;
   tScanResultHandle filtered_scan_result = NULL;
   tSirBssDescription first_ap_profile;
   VOS_STATUS ret_status = VOS_STATUS_SUCCESS;

   if (NULL == pMac) {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                 FL("pMac is NULL"));
       return VOS_STATUS_E_FAILURE;
   }
   scan_filter = vos_mem_malloc(sizeof(tCsrScanResultFilter));
   if (NULL == scan_filter) {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                 FL("scan_filter mem alloc failed"));
       return VOS_STATUS_E_FAILURE;
   } else {
      vos_mem_set(scan_filter, sizeof(tCsrScanResultFilter), 0);
      vos_mem_set(&first_ap_profile, sizeof(tSirBssDescription), 0);

      if (NULL == profile) {
          scan_filter->EncryptionType.numEntries = 1;
          scan_filter->EncryptionType.encryptionType[0]
                              = eCSR_ENCRYPT_TYPE_NONE;
      } else {
          /* Here is the profile we need to connect to */
          status = csrRoamPrepareFilterFromProfile(pMac, profile, scan_filter);
      }

      if (HAL_STATUS_SUCCESS(status)) {
          /* Save the WPS info */
          if(NULL != profile) {
             scan_filter->bWPSAssociation = profile->bWPSAssociation;
             scan_filter->bOSENAssociation = profile->bOSENAssociation;
          } else {
             scan_filter->bWPSAssociation = 0;
             scan_filter->bOSENAssociation = 0;
          }
      } else {
          VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                    FL("Preparing the profile filter failed"));
          vos_mem_free(scan_filter);
          return VOS_STATUS_E_FAILURE;
      }
   }
   status = sme_AcquireGlobalLock( &pMac->sme );
   if (eHAL_STATUS_SUCCESS == status) {
       status = csrScanGetResult(hHal, scan_filter, &filtered_scan_result);
       if (eHAL_STATUS_SUCCESS == status) {
           csr_get_bssdescr_from_scan_handle(filtered_scan_result,
                                             &first_ap_profile);
           *scan_cache = filtered_scan_result;
           if (0 != first_ap_profile.channelId) {
               *ap_chnl_id = first_ap_profile.channelId;
                VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                          FL("Found best AP and it is on channel[%d]"),
                          first_ap_profile.channelId);
           } else {
               /*
                * This means scan result is empty
                * so set the channel to zero, caller should
                * take of zero channel id case.
                */
               *ap_chnl_id = 0;
               VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                         FL("Scan result is empty, setting channel to 0"));
               ret_status = VOS_STATUS_E_FAILURE;
           }
       } else {
          VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                    FL("Failed to get scan get result"));
          ret_status = VOS_STATUS_E_FAILURE;
       }
       sme_ReleaseGlobalLock( &pMac->sme );
   } else {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                    FL("Aquiring lock failed"));
       ret_status = VOS_STATUS_E_FAILURE;
   }

   if (NULL != profile)
       csrFreeScanFilter(pMac, scan_filter);

   vos_mem_free(scan_filter);

   return ret_status;
}

/**
 * sme_store_joinreq_param() - This function will pass station's join
 *                             request to store to csr.
 * @hal_handle: pointer to hal context.
 * @profile: pointer to station's roam profile.
 * @scan_cache: pointer to station's scan cache.
 * @roam_id: reference to roam_id variable being passed.
 * @session_id: station's session id.
 *
 * This function will pass station's join request further down to csr
 * to store it. this stored parameter will be used later.
 *
 * Return: true or false based on function's overall success.
 */
bool sme_store_joinreq_param(tHalHandle hal_handle,
                             tCsrRoamProfile *profile,
                             tScanResultHandle scan_cache,
                             uint32_t *roam_id,
                             uint32_t session_id)
{
   tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal_handle);
   eHalStatus status = eHAL_STATUS_FAILURE;
   bool ret_status = true;

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                    TRACE_CODE_SME_RX_HDD_STORE_JOIN_REQ,
                    session_id, 0));
   status = sme_AcquireGlobalLock( &mac_ctx->sme );
   if (HAL_STATUS_SUCCESS(status)) {
       if (false == csr_store_joinreq_param(mac_ctx, profile, scan_cache,
                                            roam_id, session_id)) {
           ret_status = false;
       }
       sme_ReleaseGlobalLock(&mac_ctx->sme);
   } else {
       ret_status = false;
   }

   return ret_status;
}

/**
 * sme_clear_joinreq_param() - This function will pass station's clear
 *                             the join request to csr.
 * @hal_handle: pointer to hal context.
 * @session_id: station's session id.
 *
 * This function will pass station's clear join request further down to csr
 * to cleanup.
 *
 * Return: true or false based on function's overall success.
 */
bool sme_clear_joinreq_param(tHalHandle hal_handle,
                             uint32_t session_id)
{
   tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal_handle);
   eHalStatus status = eHAL_STATUS_FAILURE;
   bool ret_status = true;

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                    TRACE_CODE_SME_RX_HDD_CLEAR_JOIN_REQ,
                    session_id, 0));
   status = sme_AcquireGlobalLock( &mac_ctx->sme );
   if (HAL_STATUS_SUCCESS(status)) {
       if (false == csr_clear_joinreq_param(mac_ctx,
                                            session_id)) {
           ret_status = false;
       }
       sme_ReleaseGlobalLock(&mac_ctx->sme);
   } else {
       ret_status = false;
   }

   return ret_status;
}

/**
 * sme_issue_stored_joinreq() - This function will issues station's stored
 *                              the join request to csr.
 * @hal_handle: pointer to hal context.
 * @roam_id: reference to roam_id variable being passed.
 * @session_id: station's session id.
 *
 * This function will issue station's stored join request further down to csr
 * to proceed forward.
 *
 * Return: VOS_STATUS_SUCCESS or VOS_STATUS_E_FAILURE.
 */
VOS_STATUS sme_issue_stored_joinreq(tHalHandle hal_handle,
                                    uint32_t *roam_id,
                                    uint32_t session_id)
{
   tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal_handle);
   eHalStatus status = eHAL_STATUS_FAILURE;
   VOS_STATUS ret_status = VOS_STATUS_SUCCESS;

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                    TRACE_CODE_SME_RX_HDD_ISSUE_JOIN_REQ,
                    session_id, 0));
   status = sme_AcquireGlobalLock( &mac_ctx->sme );
   if (HAL_STATUS_SUCCESS(status)) {
       if (!HAL_STATUS_SUCCESS(csr_issue_stored_joinreq(mac_ctx,
                                                        roam_id,
                                                        session_id))) {
           ret_status = VOS_STATUS_E_FAILURE;
       }
       sme_ReleaseGlobalLock(&mac_ctx->sme);
   } else {
       csr_clear_joinreq_param(mac_ctx, session_id);
       ret_status = VOS_STATUS_E_FAILURE;
   }
   return ret_status;
}
/* ---------------------------------------------------------------------------
    \fn sme_ScanFlushResult
    \brief a wrapper function to request CSR to clear scan results.
    This is a synchronous call
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_ScanFlushResult(tHalHandle hHal, tANI_U8 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_MSG_SCAN_FLUSH_RESULTS, sessionId,0 ));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = csrScanFlushResult(hHal, sessionId);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_FilterScanResults
    \brief a wrapper function to request CSR to clear scan results.
    This is a synchronous call
    \param tHalHandle - HAL context handle
    \param sessionId - session id
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_FilterScanResults(tHalHandle hHal, tANI_U8 sessionId)
{
   eHalStatus status = eHAL_STATUS_SUCCESS;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_MSG_SCAN_FLUSH_RESULTS, sessionId,0 ));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       csrScanFilterResults(pMac);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

eHalStatus sme_ScanFlushP2PResult(tHalHandle hHal, tANI_U8 sessionId)
{
        eHalStatus status = eHAL_STATUS_FAILURE;
        tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

        MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_MSG_SCAN_FLUSH_P2PRESULTS, sessionId,0 ));
        status = sme_AcquireGlobalLock( &pMac->sme );
        if ( HAL_STATUS_SUCCESS( status ) )
        {
                status = csrScanFlushSelectiveResult( hHal, VOS_TRUE );
                sme_ReleaseGlobalLock( &pMac->sme );
        }

        return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_ScanResultGetFirst
    \brief a wrapper function to request CSR to returns the first element of
           scan result.
    This is a synchronous call
    \param hScanResult - returned from csrScanGetResult
    \return tCsrScanResultInfo * - NULL if no result
  ---------------------------------------------------------------------------*/
tCsrScanResultInfo *sme_ScanResultGetFirst(tHalHandle hHal,
                                          tScanResultHandle hScanResult)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tCsrScanResultInfo *pRet = NULL;

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_MSG_SCAN_RESULT_GETFIRST, NO_SESSION,0 ));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       pRet = csrScanResultGetFirst( pMac, hScanResult );
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (pRet);
}


/* ---------------------------------------------------------------------------
    \fn sme_ScanResultGetNext
    \brief a wrapper function to request CSR to returns the next element of
           scan result. It can be called without calling csrScanResultGetFirst
           first
    This is a synchronous call
    \param hScanResult - returned from csrScanGetResult
    \return Null if no result or reach the end
  ---------------------------------------------------------------------------*/
tCsrScanResultInfo *sme_ScanResultGetNext(tHalHandle hHal,
                                          tScanResultHandle hScanResult)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    tCsrScanResultInfo *pRet = NULL;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        pRet = csrScanResultGetNext( pMac, hScanResult );
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (pRet);
}


/* ---------------------------------------------------------------------------
    \fn sme_ScanSetBGScanparams
    \brief a wrapper function to request CSR to set BG scan params in PE
    This is a synchronous call
    \param pScanReq - BG scan request structure
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_ScanSetBGScanparams(tHalHandle hHal, tANI_U8 sessionId, tCsrBGScanRequest *pScanReq)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    if( NULL != pScanReq )
    {
        status = sme_AcquireGlobalLock( &pMac->sme );
        if ( HAL_STATUS_SUCCESS( status ) )
        {
            status = csrScanSetBGScanparams( hHal, pScanReq );
            sme_ReleaseGlobalLock( &pMac->sme );
        }
    }

    return (status);
}


/* ---------------------------------------------------------------------------
    \fn sme_ScanResultPurge
    \brief a wrapper function to request CSR to remove all items(tCsrScanResult)
           in the list and free memory for each item
    This is a synchronous call
    \param hScanResult - returned from csrScanGetResult. hScanResult is
                         considered gone by
    calling this function and even before this function returns.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_ScanResultPurge(tHalHandle hHal, tScanResultHandle hScanResult)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_MSG_SCAN_RESULT_PURGE, NO_SESSION,0 ));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = csrScanResultPurge( hHal, hScanResult );
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_ScanGetPMKIDCandidateList
    \brief a wrapper function to return the PMKID candidate list
    This is a synchronous call
    \param pPmkidList - caller allocated buffer point to an array of
                        tPmkidCandidateInfo
    \param pNumItems - pointer to a variable that has the number of
                       tPmkidCandidateInfo allocated when returning, this is
                       either the number needed or number of items put into
                       pPmkidList
    \return eHalStatus - when fail, it usually means the buffer allocated is not
                         big enough and pNumItems
    has the number of tPmkidCandidateInfo.
    \Note: pNumItems is a number of tPmkidCandidateInfo,
           not sizeof(tPmkidCandidateInfo) * something
  ---------------------------------------------------------------------------*/
eHalStatus sme_ScanGetPMKIDCandidateList(tHalHandle hHal, tANI_U8 sessionId,
                                        tPmkidCandidateInfo *pPmkidList,
                                        tANI_U32 *pNumItems )
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = csrScanGetPMKIDCandidateList( pMac, sessionId, pPmkidList, pNumItems );
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/*----------------------------------------------------------------------------
  \fn sme_RoamRegisterLinkQualityIndCallback

  \brief
  a wrapper function to allow HDD to register a callback handler with CSR for
  link quality indications.

  Only one callback may be registered at any time.
  In order to deregister the callback, a NULL cback may be provided.

  Registration happens in the task context of the caller.

  \param callback - Call back being registered
  \param pContext - user data

  DEPENDENCIES: After CSR open

  \return eHalStatus
-----------------------------------------------------------------------------*/
eHalStatus sme_RoamRegisterLinkQualityIndCallback(tHalHandle hHal, tANI_U8 sessionId,
                                                  csrRoamLinkQualityIndCallback   callback,
                                                  void                           *pContext)
{
   return(csrRoamRegisterLinkQualityIndCallback((tpAniSirGlobal)hHal, callback, pContext));
}

eCsrPhyMode sme_GetPhyMode(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    return pMac->roam.configParam.phyMode;
}

/* ---------------------------------------------------------------------------
    \fn sme_GetChannelBondingMode5G
    \brief get the channel bonding mode for 5G band
    \param hHal - HAL handle
    \return channel bonding mode for 5G
  ---------------------------------------------------------------------------*/
tANI_U32 sme_GetChannelBondingMode5G(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tSmeConfigParams  smeConfig;

    sme_GetConfigParam(pMac, &smeConfig);

    return smeConfig.csrConfig.channelBondingMode5GHz;
}

/* ---------------------------------------------------------------------------
    \fn sme_GetChannelBondingMode24G
    \brief get the channel bonding mode for 2.4G band
    \param hHal - HAL handle
    \return channel bonding mode for 2.4G
  ---------------------------------------------------------------------------*/
tANI_U32 sme_GetChannelBondingMode24G(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tSmeConfigParams  smeConfig;

    sme_GetConfigParam(pMac, &smeConfig);

    return smeConfig.csrConfig.channelBondingMode24GHz;
}


/* ---------------------------------------------------------------------------
    \fn sme_RoamConnect
    \brief a wrapper function to request CSR to initiate an association
    This is an asynchronous call.
    \param sessionId - the sessionId returned by sme_OpenSession.
    \param pProfile - description of the network to which to connect
    \param hBssListIn - a list of BSS descriptor to roam to. It is returned
                        from csrScanGetResult
    \param pRoamId - to get back the request ID
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamConnect(tHalHandle hHal, tANI_U8 sessionId, tCsrRoamProfile *pProfile,
                           tANI_U32 *pRoamId)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    if (!pMac)
    {
        return eHAL_STATUS_FAILURE;
    }

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_MSG_CONNECT, sessionId, 0));
    smsLog(pMac, LOG2, FL("enter"));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
        {
            status = csrRoamConnect( pMac, sessionId, pProfile, NULL, pRoamId );
        }
        else
        {
            smsLog(pMac, LOGE, FL("invalid sessionID %d"), sessionId);
            status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    else
    {
        smsLog(pMac, LOGE, FL("sme_AcquireGlobalLock failed"));
    }

    return (status);
}

/* ---------------------------------------------------------------------------

    \fn sme_SetPhyMode

    \brief Changes the PhyMode.

    \param hHal - The handle returned by macOpen.

    \param phyMode new phyMode which is to set

    \return eHalStatus  SUCCESS.

  -------------------------------------------------------------------------------*/
eHalStatus sme_SetPhyMode(tHalHandle hHal, eCsrPhyMode phyMode)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    if (NULL == pMac)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: invalid context", __func__);
        return eHAL_STATUS_FAILURE;
    }

    pMac->roam.configParam.phyMode = phyMode;
    pMac->roam.configParam.uCfgDot11Mode = csrGetCfgDot11ModeFromCsrPhyMode(NULL,
                                                 pMac->roam.configParam.phyMode,
                                    pMac->roam.configParam.ProprietaryRatesEnabled);

    return eHAL_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamReassoc
    \brief a wrapper function to request CSR to initiate a re-association
    \param pProfile - can be NULL to join the currently connected AP. In that
    case modProfileFields should carry the modified field(s) which could trigger
    reassoc
    \param modProfileFields - fields which are part of tCsrRoamConnectedProfile
    that might need modification dynamically once STA is up & running and this
    could trigger a reassoc
    \param pRoamId - to get back the request ID
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamReassoc(tHalHandle hHal, tANI_U8 sessionId, tCsrRoamProfile *pProfile,
                          tCsrRoamModifyProfileFields modProfileFields,
                          tANI_U32 *pRoamId, v_BOOL_t fForce)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                      TRACE_CODE_SME_RX_HDD_ROAM_REASSOC, sessionId, 0));
    smsLog(pMac, LOG2, FL("enter"));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
        {
            if((NULL == pProfile) && (fForce == 1))
            {
                status = csrReassoc( pMac, sessionId, &modProfileFields, pRoamId , fForce);
            }
            else
            {
                status = csrRoamReassoc( pMac, sessionId, pProfile, modProfileFields, pRoamId );
            }
        }
        else
        {
            status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamConnectToLastProfile
    \brief a wrapper function to request CSR to disconnect and reconnect with
           the same profile
    This is an asynchronous call.
    \return eHalStatus. It returns fail if currently connected
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamConnectToLastProfile(tHalHandle hHal, tANI_U8 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                TRACE_CODE_SME_RX_HDD_ROAM_GET_CONNECTPROFILE, sessionId, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
      {
         status = csrRoamConnectToLastProfile( pMac, sessionId );
      }
      else
      {
          status = eHAL_STATUS_INVALID_PARAMETER;
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamDisconnect
    \brief a wrapper function to request CSR to disconnect from a network
    This is an asynchronous call.
    \param reason -- To indicate the reason for disconnecting. Currently, only
                     eCSR_DISCONNECT_REASON_MIC_ERROR is meaningful.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamDisconnect(tHalHandle hHal, tANI_U8 sessionId, eCsrRoamDisconnectReason reason)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                        TRACE_CODE_SME_RX_HDD_ROAM_DISCONNECT, sessionId, reason));
   smsLog(pMac, LOG2, FL("enter"));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
      {
         status = csrRoamDisconnect( pMac, sessionId, reason );
      }
      else
      {
          status = eHAL_STATUS_INVALID_PARAMETER;
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamStopBss
    \brief To stop BSS for Soft AP. This is an asynchronous API.
    \param hHal - Global structure
    \param sessionId - sessionId of SoftAP
    \return eHalStatus  SUCCESS  Roam callback will be called to indicate actual results
  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamStopBss(tHalHandle hHal, tANI_U8 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   smsLog(pMac, LOG2, FL("enter"));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
      {
         status = csrRoamIssueStopBssCmd( pMac, sessionId, eANI_BOOLEAN_TRUE );
      }
      else
      {
          status = eHAL_STATUS_INVALID_PARAMETER;
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamDisconnectSta
    \brief To disassociate a station. This is an asynchronous API.
    \param hHal - Global structure
    \param sessionId - sessionId of SoftAP
    \param pPeerMacAddr - Caller allocated memory filled with peer MAC address (6 bytes)
    \return eHalStatus  SUCCESS  Roam callback will be called to indicate actual results
  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamDisconnectSta(tHalHandle hHal, tANI_U8 sessionId,
                                struct tagCsrDelStaParams *pDelStaParams)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if ( NULL == pMac )
   {
     VOS_ASSERT(0);
     return status;
   }

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
      {
         status = csrRoamIssueDisassociateStaCmd( pMac, sessionId, pDelStaParams);
      }
      else
      {
         status = eHAL_STATUS_INVALID_PARAMETER;
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamDeauthSta
    \brief To disassociate a station. This is an asynchronous API.
    \param hHal - Global structure
    \param sessionId - sessionId of SoftAP
    \param pDelStaParams -Pointer to parameters of the station to deauthenticate
    \return eHalStatus  SUCCESS  Roam callback will be called to indicate actual results
  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamDeauthSta(tHalHandle hHal, tANI_U8 sessionId,
                             struct tagCsrDelStaParams *pDelStaParams)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if ( NULL == pMac )
   {
     VOS_ASSERT(0);
     return status;
   }

   MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_HDD_MSG_DEAUTH_STA,
                         sessionId, pDelStaParams->reason_code));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
      {
         status = csrRoamIssueDeauthStaCmd( pMac, sessionId, pDelStaParams);
      }
      else
      {
         status = eHAL_STATUS_INVALID_PARAMETER;
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamTKIPCounterMeasures
    \brief To start or stop TKIP counter measures. This is an asynchronous API.
    \param sessionId - sessionId of SoftAP
    \param pPeerMacAddr - Caller allocated memory filled with peer MAC address (6 bytes)
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamTKIPCounterMeasures(tHalHandle hHal, tANI_U8 sessionId,
                                        tANI_BOOLEAN bEnable)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if ( NULL == pMac )
   {
     VOS_ASSERT(0);
     return status;
   }

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
        if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
        {
            status = csrRoamIssueTkipCounterMeasures( pMac, sessionId, bEnable);
        }
        else
        {
            status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamGetAssociatedStas
    \brief To probe the list of associated stations from various modules of CORE stack.
    \This is an asynchronous API.
    \param sessionId    - sessionId of SoftAP
    \param modId        - Module from whom list of associated stations is to be probed.
                          If an invalid module is passed then by default VOS_MODULE_ID_PE will be probed
    \param pUsrContext  - Opaque HDD context
    \param pfnSapEventCallback  - Sap event callback in HDD
    \param pAssocBuf    - Caller allocated memory to be filled with
                          associated stations info
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamGetAssociatedStas(tHalHandle hHal, tANI_U8 sessionId,
                                        VOS_MODULE_ID modId, void *pUsrContext,
                                        void *pfnSapEventCallback, tANI_U8 *pAssocStasBuf)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if ( NULL == pMac )
   {
     VOS_ASSERT(0);
     return status;
   }

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
        if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
        {
            status = csrRoamGetAssociatedStas( pMac, sessionId, modId, pUsrContext, pfnSapEventCallback, pAssocStasBuf );
        }
        else
        {
            status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamGetWpsSessionOverlap
    \brief To get the WPS PBC session overlap information.
    \This is an asynchronous API.
    \param sessionId    - sessionId of SoftAP
    \param pUsrContext  - Opaque HDD context
    \param pfnSapEventCallback  - Sap event callback in HDD
    \pRemoveMac - pointer to Mac address which needs to be removed from session
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamGetWpsSessionOverlap(tHalHandle hHal, tANI_U8 sessionId,
                                        void *pUsrContext, void
                                        *pfnSapEventCallback, v_MACADDR_t pRemoveMac)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if ( NULL == pMac )
   {
     VOS_ASSERT(0);
     return status;
   }

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
        if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
        {
            status = csrRoamGetWpsSessionOverlap( pMac, sessionId, pUsrContext, pfnSapEventCallback, pRemoveMac);
        }
        else
        {
            status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}


/* ---------------------------------------------------------------------------
    \fn sme_RoamGetConnectState
    \brief a wrapper function to request CSR to return the current connect state
           of Roaming
    This is a synchronous call.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamGetConnectState(tHalHandle hHal, tANI_U8 sessionId, eCsrConnectState *pState)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
       if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
       {
          status = csrRoamGetConnectState( pMac, sessionId, pState );
       }
       else
       {
           status = eHAL_STATUS_INVALID_PARAMETER;
       }
       sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamGetConnectProfile
    \brief a wrapper function to request CSR to return the current connect
           profile. Caller must call csrRoamFreeConnectProfile after it is done
           and before reuse for another csrRoamGetConnectProfile call.
    This is a synchronous call.
    \param pProfile - pointer to a caller allocated structure
                      tCsrRoamConnectedProfile
    \return eHalStatus. Failure if not connected
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamGetConnectProfile(tHalHandle hHal, tANI_U8 sessionId,
                                     tCsrRoamConnectedProfile *pProfile)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                TRACE_CODE_SME_RX_HDD_ROAM_GET_CONNECTPROFILE, sessionId, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
      {
         status = csrRoamGetConnectProfile( pMac, sessionId, pProfile );
      }
      else
      {
          status = eHAL_STATUS_INVALID_PARAMETER;
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamFreeConnectProfile
    \brief a wrapper function to request CSR to free and reinitialize the
           profile returned previously by csrRoamGetConnectProfile.
    This is a synchronous call.
    \param pProfile - pointer to a caller allocated structure
                      tCsrRoamConnectedProfile
    \return eHalStatus.
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamFreeConnectProfile(tHalHandle hHal,
                                      tCsrRoamConnectedProfile *pProfile)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
            TRACE_CODE_SME_RX_HDD_ROAM_FREE_CONNECTPROFILE, NO_SESSION, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = csrRoamFreeConnectProfile( pMac, pProfile );
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamSetPMKIDCache
    \brief a wrapper function to request CSR to return the PMKID candidate list
    This is a synchronous call.
    \param pPMKIDCache - caller allocated buffer point to an array of
                         tPmkidCacheInfo
    \param numItems - a variable that has the number of tPmkidCacheInfo
                      allocated when returning, this is either the number needed
                      or number of items put into pPMKIDCache
    \param update_entire_cache - this bool value specifies if the entire pmkid
                               cache should be overwritten or should it be
                               updated entry by entry.
    \return eHalStatus - when fail, it usually means the buffer allocated is not
                         big enough and pNumItems has the number of
                         tPmkidCacheInfo.
    \Note: pNumItems is a number of tPmkidCacheInfo,
           not sizeof(tPmkidCacheInfo) * something
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamSetPMKIDCache( tHalHandle hHal, tANI_U8 sessionId,
                                  tPmkidCacheInfo *pPMKIDCache,
                                  tANI_U32 numItems,
                                  tANI_BOOLEAN update_entire_cache )
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_ROAM_SET_PMKIDCACHE, sessionId, numItems));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
      {
         status = csrRoamSetPMKIDCache( pMac, sessionId, pPMKIDCache,
                                        numItems, update_entire_cache);
      }
      else
      {
          status = eHAL_STATUS_INVALID_PARAMETER;
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

eHalStatus sme_RoamDelPMKIDfromCache( tHalHandle hHal, tANI_U8 sessionId,
                                      const tANI_U8 *pBSSId,
                                      tANI_BOOLEAN flush_cache )
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_ROAM_DEL_PMKIDCACHE, sessionId, flush_cache));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
      {
         status = csrRoamDelPMKIDfromCache( pMac, sessionId,
                                            pBSSId, flush_cache );
      }
      else
      {
          status = eHAL_STATUS_INVALID_PARAMETER;
      }
      sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
/* ---------------------------------------------------------------------------
 *\fn sme_RoamSetPSK_PMK
 *\brief a wrapper function to request CSR to save PSK/PMK
 * This is a synchronous call.
 *\param hHal - Global structure
 *\param sessionId - SME sessionId
 *\param pPSK_PMK - pointer to an array of Psk[]/Pmk
 *\param pmk_len - Length could be only 16 bytes in case if LEAP
                   connections. Need to pass this information to
                   firmware.
 *\return eHalStatus -status whether PSK/PMK is set or not
 *---------------------------------------------------------------------------*/
eHalStatus sme_RoamSetPSK_PMK (tHalHandle hHal, tANI_U8 sessionId,
                               tANI_U8 *pPSK_PMK, size_t pmk_len)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    status = sme_AcquireGlobalLock(&pMac->sme);
    if (HAL_STATUS_SUCCESS(status)) {
        if (CSR_IS_SESSION_VALID(pMac, sessionId)) {
            status = csrRoamSetPSK_PMK(pMac, sessionId, pPSK_PMK, pmk_len);
        }
        else {
            status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return (status);
}
#endif
/* ---------------------------------------------------------------------------
    \fn sme_RoamGetSecurityReqIE
    \brief a wrapper function to request CSR to return the WPA or RSN or WAPI IE CSR
           passes to PE to JOIN request or START_BSS request
    This is a synchronous call.
    \param pLen - caller allocated memory that has the length of pBuf as input.
                  Upon returned, *pLen has the needed or IE length in pBuf.
    \param pBuf - Caller allocated memory that contain the IE field, if any,
                  upon return
    \param secType - Specifies whether looking for WPA/WPA2/WAPI IE
    \return eHalStatus - when fail, it usually means the buffer allocated is not
                         big enough
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamGetSecurityReqIE(tHalHandle hHal, tANI_U8 sessionId, tANI_U32 *pLen,
                                  tANI_U8 *pBuf, eCsrSecurityType secType)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
        {
           status = csrRoamGetWpaRsnReqIE( hHal, sessionId, pLen, pBuf );
        }
        else
        {
           status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamGetSecurityRspIE
    \brief a wrapper function to request CSR to return the WPA or RSN or WAPI IE from
           the beacon or probe rsp if connected
    This is a synchronous call.
    \param pLen - caller allocated memory that has the length of pBuf as input.
                  Upon returned, *pLen has the needed or IE length in pBuf.
    \param pBuf - Caller allocated memory that contain the IE field, if any,
                  upon return
    \param secType - Specifies whether looking for WPA/WPA2/WAPI IE
    \return eHalStatus - when fail, it usually means the buffer allocated is not
                         big enough
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamGetSecurityRspIE(tHalHandle hHal, tANI_U8 sessionId, tANI_U32 *pLen,
                                  tANI_U8 *pBuf, eCsrSecurityType secType)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
        {
           status = csrRoamGetWpaRsnRspIE( pMac, sessionId, pLen, pBuf );
        }
        else
        {
           status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);

}


/* ---------------------------------------------------------------------------
    \fn sme_RoamGetNumPMKIDCache
    \brief a wrapper function to request CSR to return number of PMKID cache
           entries
    This is a synchronous call.
    \return tANI_U32 - the number of PMKID cache entries
  ---------------------------------------------------------------------------*/
tANI_U32 sme_RoamGetNumPMKIDCache(tHalHandle hHal, tANI_U8 sessionId)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    tANI_U32 numPmkidCache = 0;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
        {
           numPmkidCache = csrRoamGetNumPMKIDCache( pMac, sessionId );
           status = eHAL_STATUS_SUCCESS;
        }
        else
        {
           status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (numPmkidCache);
}

/* ---------------------------------------------------------------------------
    \fn sme_RoamGetPMKIDCache
    \brief a wrapper function to request CSR to return PMKID cache from CSR
    This is a synchronous call.
    \param pNum - caller allocated memory that has the space of the number of
                  pBuf tPmkidCacheInfo as input. Upon returned, *pNum has the
                  needed or actually number in tPmkidCacheInfo.
    \param pPmkidCache - Caller allocated memory that contains PMKID cache, if
                         any, upon return
    \return eHalStatus - when fail, it usually means the buffer allocated is not
                         big enough
  ---------------------------------------------------------------------------*/
eHalStatus sme_RoamGetPMKIDCache(tHalHandle hHal, tANI_U8 sessionId, tANI_U32 *pNum,
                                 tPmkidCacheInfo *pPmkidCache)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_ROAM_GET_PMKIDCACHE, sessionId, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
       {
          status = csrRoamGetPMKIDCache( pMac, sessionId, pNum, pPmkidCache );
       }
       else
       {
          status = eHAL_STATUS_INVALID_PARAMETER;
       }
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}


/* ---------------------------------------------------------------------------
    \fn sme_GetConfigParam
    \brief a wrapper function that HDD calls to get the global settings
           currently maintained by CSR.
    This is a synchronous call.
    \param pParam - caller allocated memory
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_GetConfigParam(tHalHandle hHal, tSmeConfigParams *pParam)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
            TRACE_CODE_SME_RX_HDD_GET_CONFIGPARAM, NO_SESSION, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      status = csrGetConfigParam(pMac, &pParam->csrConfig);
      if (status != eHAL_STATUS_SUCCESS)
      {
         smsLog( pMac, LOGE, "%s csrGetConfigParam failed", __func__);
         sme_ReleaseGlobalLock( &pMac->sme );
         return status;
      }
#ifdef FEATURE_AP_MCC_CH_AVOIDANCE
      pParam->sap_channel_avoidance = pMac->sap.sap_channel_avoidance;
#endif /* FEATURE_AP_MCC_CH_AVOIDANCE */
      pParam->fScanOffload = pMac->fScanOffload;
      pParam->fP2pListenOffload = pMac->fP2pListenOffload;
      pParam->pnoOffload = pMac->pnoOffload;
      pParam->max_intf_count = pMac->sme.max_intf_count;
      pParam->enableSelfRecovery = pMac->sme.enableSelfRecovery;
      pParam->f_prefer_non_dfs_on_radar = pMac->f_prefer_non_dfs_on_radar;
      pParam->fine_time_meas_cap = pMac->fine_time_meas_cap;
      pParam->csrConfig.mcc_rts_cts_prot_enable =
              pMac->roam.configParam.mcc_rts_cts_prot_enable;
      pParam->csrConfig.mcc_bcast_prob_resp_enable =
              pMac->roam.configParam.mcc_bcast_prob_resp_enable;

      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_CfgSetInt
    \brief a wrapper function that HDD calls to set parameters in CFG.
    This is a synchronous call.
    \param cfgId - Configuration Parameter ID (type) for STA.
    \param ccmValue - The information related to Configuration Parameter ID
                      which needs to be saved in CFG
    \param callback - To be registered by CSR with CCM. Once the CFG done with
                      saving the information in the database, it notifies CCM &
                      then the callback will be invoked to notify.
    \param toBeSaved - To save the request for future reference
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_CfgSetInt(tHalHandle hHal, tANI_U32 cfgId, tANI_U32 ccmValue,
                         tCcmCfgSetCallback callback, eAniBoolean toBeSaved)
{
   return(ccmCfgSetInt(hHal, cfgId, ccmValue, callback, toBeSaved));
}

/* ---------------------------------------------------------------------------
    \fn sme_CfgSetStr
    \brief a wrapper function that HDD calls to set parameters in CFG.
    This is a synchronous call.
    \param cfgId - Configuration Parameter ID (type) for STA.
    \param pStr - Pointer to the byte array which carries the information needs
                  to be saved in CFG
    \param length - Length of the data to be saved
    \param callback - To be registered by CSR with CCM. Once the CFG done with
                      saving the information in the database, it notifies CCM &
                      then the callback will be invoked to notify.
    \param toBeSaved - To save the request for future reference
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_CfgSetStr(tHalHandle hHal, tANI_U32 cfgId, tANI_U8 *pStr,
                         tANI_U32 length, tCcmCfgSetCallback callback,
                         eAniBoolean toBeSaved)
{
   return(ccmCfgSetStr(hHal, cfgId, pStr, length, callback, toBeSaved));
}

/* ---------------------------------------------------------------------------
    \fn sme_GetModifyProfileFields
    \brief HDD or SME - QOS calls this function to get the current values of
    connected profile fields, changing which can cause reassoc.
    This function must be called after CFG is downloaded and STA is in connected
    state. Also, make sure to call this function to get the current profile
    fields before calling the reassoc. So that pModifyProfileFields will have
    all the latest values plus the one(s) has been updated as part of reassoc
    request.
    \param pModifyProfileFields - pointer to the connected profile fields
    changing which can cause reassoc

    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_GetModifyProfileFields(tHalHandle hHal, tANI_U8 sessionId,
                                     tCsrRoamModifyProfileFields * pModifyProfileFields)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_GET_MODPROFFIELDS, sessionId, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       if( CSR_IS_SESSION_VALID( pMac, sessionId ) )
       {
          status = csrGetModifyProfileFields(pMac, sessionId, pModifyProfileFields);
       }
       else
       {
          status = eHAL_STATUS_INVALID_PARAMETER;
       }
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/*--------------------------------------------------------------------------
    \fn sme_SetConfigPowerSave
    \brief  Wrapper fn to change power save configuration in SME (PMC) module.
            For BMPS related configuration, this function also updates the CFG
            and sends a message to FW to pick up the new values. Note: Calling
            this function only updates the configuration and does not enable
            the specified power save mode.
    \param  hHal - The handle returned by macOpen.
    \param  psMode - Power Saving mode being modified
    \param  pConfigParams - a pointer to a caller allocated object of type
            tPmcSmpsConfigParams or tPmcBmpsConfigParams or tPmcImpsConfigParams
    \return eHalStatus
  --------------------------------------------------------------------------*/
eHalStatus sme_SetConfigPowerSave(tHalHandle hHal, tPmcPowerSavingMode psMode,
                                  void *pConfigParams)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                 TRACE_CODE_SME_RX_HDD_SET_CONFIG_PWRSAVE, NO_SESSION, 0));
   if (NULL == pConfigParams ) {
      smsLog( pMac, LOGE, "Empty config param structure for PMC, "
              "nothing to update");
      return eHAL_STATUS_FAILURE;
   }

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcSetConfigPowerSave(hHal, psMode, pConfigParams);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/*--------------------------------------------------------------------------
    \fn sme_GetConfigPowerSave
    \brief  Wrapper fn to retrieve power save configuration in SME (PMC) module
    \param  hHal - The handle returned by macOpen.
    \param  psMode - Power Saving mode
    \param  pConfigParams - a pointer to a caller allocated object of type
            tPmcSmpsConfigParams or tPmcBmpsConfigParams or tPmcImpsConfigParams
    \return eHalStatus
  --------------------------------------------------------------------------*/
eHalStatus sme_GetConfigPowerSave(tHalHandle hHal, tPmcPowerSavingMode psMode,
                                  void *pConfigParams)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
            TRACE_CODE_SME_RX_HDD_GET_CONFIG_PWRSAVE, NO_SESSION, 0));
   if (NULL == pConfigParams ) {
      smsLog( pMac, LOGE, "Empty config param structure for PMC, "
              "nothing to update");
      return eHAL_STATUS_FAILURE;
   }

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcGetConfigPowerSave(hHal, psMode, pConfigParams);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_EnablePowerSave
    \brief  Enables one of the power saving modes.
    \param  hHal - The handle returned by macOpen.
    \param  psMode - The power saving mode to enable. If BMPS mode is enabled
                     while the chip is operating in Full Power, PMC will start
                     a timer that will try to put the chip in BMPS mode after
                     expiry.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_EnablePowerSave (tHalHandle hHal, tPmcPowerSavingMode psMode)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_ENABLE_PWRSAVE, NO_SESSION, psMode));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status =  pmcEnablePowerSave(hHal, psMode);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_DisablePowerSave
    \brief   Disables one of the power saving modes.
    \param  hHal - The handle returned by macOpen.
    \param  psMode - The power saving mode to disable. Disabling does not imply
                     that device will be brought out of the current PS mode. This
                     is purely a configuration API.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_DisablePowerSave (tHalHandle hHal, tPmcPowerSavingMode psMode)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_DISABLE_PWRSAVE, NO_SESSION, psMode));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcDisablePowerSave(hHal, psMode);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
 }

/* ---------------------------------------------------------------------------
+    \fn sme_SetHostPowerSave
+    \brief   Enables BMPS logic to be controlled by User level apps
+    \param  hHal - The handle returned by macOpen.
+    \param  psMode - The power saving mode to disable. Disabling does not imply
+                     that device will be brought out of the current PS mode. This
+                     is purely a configuration API.
+    \return eHalStatus
+  ---------------------------------------------------------------------------*/
eHalStatus sme_SetHostPowerSave (tHalHandle hHal, v_BOOL_t psMode)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   pMac->pmc.isHostPsEn = psMode;

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_StartAutoBmpsTimer
    \brief  Starts a timer that periodically polls all the registered
            module for entry into Bmps mode. This timer is started only if BMPS is
            enabled and whenever the device is in full power.
    \param  hHal - The handle returned by macOpen.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_StartAutoBmpsTimer ( tHalHandle hHal)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_START_AUTO_BMPSTIMER, NO_SESSION, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcStartAutoBmpsTimer(hHal);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}
/* ---------------------------------------------------------------------------
    \fn sme_StopAutoBmpsTimer
    \brief  Stops the Auto BMPS Timer that was started using sme_startAutoBmpsTimer
            Stopping the timer does not cause a device state change. Only the timer
            is stopped. If "Full Power" is desired, use the sme_RequestFullPower API
    \param  hHal - The handle returned by macOpen.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_StopAutoBmpsTimer ( tHalHandle hHal)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_STOP_AUTO_BMPSTIMER, NO_SESSION, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcStopAutoBmpsTimer(hHal);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}
/**
 * sme_process_set_max_tx_power() - Set the Maximum Transmit Power
 *
 * @pMac: mac pointer.
 * @command: cmd param containing bssid, self mac
 *           and power in db
 *
 * Set the maximum transmit power dynamically.
 *
 * Return: eHalStatus
 *
 */
eHalStatus sme_process_set_max_tx_power(tpAniSirGlobal pMac,
						tSmeCmd *command)
{
	vos_msg_t msg;
	tMaxTxPowerParams *max_tx_params = NULL;

	max_tx_params = vos_mem_malloc(sizeof(*max_tx_params));
	if (NULL == max_tx_params)
	{
		smsLog(pMac, LOGE, FL("fail to allocate memory for max_tx_params"));
		return eHAL_STATUS_FAILURE;
	}

	vos_mem_copy(max_tx_params->bssId,
		command->u.set_tx_max_pwr.bssid, SIR_MAC_ADDR_LENGTH);
	vos_mem_copy(max_tx_params->selfStaMacAddr,
		command->u.set_tx_max_pwr.self_sta_mac_addr,
				SIR_MAC_ADDR_LENGTH);
	max_tx_params->power =
			command->u.set_tx_max_pwr.power;

	msg.type = WDA_SET_MAX_TX_POWER_REQ;
	msg.reserved = 0;
	msg.bodyptr = max_tx_params;

	if(VOS_STATUS_SUCCESS !=
		vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))
	{
		smsLog(pMac, LOGE,
			FL("Not able to post WDA_SET_MAX_TX_POWER_REQ message to WDA"));
		vos_mem_free(max_tx_params);
		return eHAL_STATUS_FAILURE;
	}
	return eHAL_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------
    \fn sme_QueryPowerState
    \brief  Returns the current power state of the device.
    \param  hHal - The handle returned by macOpen.
    \param pPowerState - pointer to location to return power state (LOW or HIGH)
    \param pSwWlanSwitchState - ptr to location to return SW WLAN Switch state
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_QueryPowerState (
   tHalHandle hHal,
   tPmcPowerState *pPowerState,
   tPmcSwitchState *pSwWlanSwitchState)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcQueryPowerState (hHal, pPowerState, NULL, pSwWlanSwitchState);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_IsPowerSaveEnabled
    \brief  Checks if the device is able to enter a particular power save mode
            This does not imply that the device is in a particular PS mode
    \param  hHal - The handle returned by macOpen.
    \param  sessionId - sme session id
    \param psMode - the power saving mode
    \return eHalStatus
  ---------------------------------------------------------------------------*/
tANI_BOOLEAN sme_IsPowerSaveEnabled (tHalHandle hHal,
                                     tANI_U32 sessionId,
                                     tPmcPowerSavingMode psMode)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tANI_BOOLEAN result = false;

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
               TRACE_CODE_SME_RX_HDD_IS_PWRSAVE_ENABLED, NO_SESSION, psMode));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       if(!pMac->psOffloadEnabled)
          result = pmcIsPowerSaveEnabled(hHal, psMode);
       else
          result = pmcOffloadIsPowerSaveEnabled(hHal, sessionId, psMode);
       sme_ReleaseGlobalLock( &pMac->sme );
       return result;
   }

   return false;
}

/* ---------------------------------------------------------------------------
    \fn sme_RequestFullPower
    \brief  Request that the device be brought to full power state. When the
            device enters Full Power PMC will start a BMPS timer if BMPS PS mode
            is enabled. On timer expiry PMC will attempt to put the device in
            BMPS mode if following holds true:
            - BMPS mode is enabled
            - Polling of all modules through the Power Save Check routine passes
            - STA is associated to an access point
    \param  hHal - The handle returned by macOpen.
    \param  - callbackRoutine Callback routine invoked in case of success/failure
    \return eHalStatus - status
     eHAL_STATUS_SUCCESS - device brought to full power state
     eHAL_STATUS_FAILURE - device cannot be brought to full power state
     eHAL_STATUS_PMC_PENDING - device is being brought to full power state,
  ---------------------------------------------------------------------------*/
eHalStatus sme_RequestFullPower (
   tHalHandle hHal,
   void (*callbackRoutine) (void *callbackContext, eHalStatus status),
   void *callbackContext,
   tRequestFullPowerReason fullPowerReason)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
         TRACE_CODE_SME_RX_HDD_REQUEST_FULLPOWER, NO_SESSION, fullPowerReason));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcRequestFullPower(hHal, callbackRoutine, callbackContext, fullPowerReason);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RequestBmps
    \brief  Request that the device be put in BMPS state. Request will be
            accepted only if BMPS mode is enabled and power save check routine
            passes.
    \param  hHal - The handle returned by macOpen.
    \param  - callbackRoutine Callback routine invoked in case of success/failure
    \return eHalStatus
      eHAL_STATUS_SUCCESS - device is in BMPS state
      eHAL_STATUS_FAILURE - device cannot be brought to BMPS state
      eHAL_STATUS_PMC_PENDING - device is being brought to BMPS state
  ---------------------------------------------------------------------------*/
eHalStatus sme_RequestBmps (
   tHalHandle hHal,
   void (*callbackRoutine) (void *callbackContext, eHalStatus status),
   void *callbackContext)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_REQUEST_BMPS, NO_SESSION, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcRequestBmps(hHal, callbackRoutine, callbackContext);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}


/* ---------------------------------------------------------------------------
    \fn  sme_SetDHCPTillPowerActiveFlag
    \brief  Sets/Clears DHCP related flag in PMC to disable/enable auto BMPS
            entry by PMC
    \param  hHal - The handle returned by macOpen.
  ---------------------------------------------------------------------------*/
void  sme_SetDHCPTillPowerActiveFlag(tHalHandle hHal, tANI_U8 flag)
{
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_SET_DHCP_FLAG, NO_SESSION, flag));
   /* Set/Clear the DHCP flag which will disable/enable
      auto BMPS enter by PMC */
   pMac->pmc.remainInPowerActiveTillDHCP = flag;
}


/* ---------------------------------------------------------------------------
    \fn sme_StartUapsd
    \brief  Request that the device be put in UAPSD state. If the device is in
            Full Power it will be put in BMPS mode first and then into UAPSD
            mode.
    \param  hHal - The handle returned by macOpen.
    \param  - callbackRoutine Callback routine invoked in case of success/failure
      eHAL_STATUS_SUCCESS - device is in UAPSD state
      eHAL_STATUS_FAILURE - device cannot be brought to UAPSD state
      eHAL_STATUS_PMC_PENDING - device is being brought to UAPSD state
      eHAL_STATUS_PMC_DISABLED - UAPSD is disabled or BMPS mode is disabled
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_StartUapsd (
   tHalHandle hHal,
   void (*callbackRoutine) (void *callbackContext, eHalStatus status),
   void *callbackContext)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcStartUapsd(hHal, callbackRoutine, callbackContext);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
 }

/* ---------------------------------------------------------------------------
    \fn sme_StopUapsd
    \brief  Request that the device be put out of UAPSD state. Device will be
            put in in BMPS state after stop UAPSD completes.
    \param  hHal - The handle returned by macOpen.
    \return eHalStatus
      eHAL_STATUS_SUCCESS - device is put out of UAPSD and back in BMPS state
      eHAL_STATUS_FAILURE - device cannot be brought out of UAPSD state
  ---------------------------------------------------------------------------*/
eHalStatus sme_StopUapsd (tHalHandle hHal)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcStopUapsd(hHal);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RequestStandby
    \brief  Request that the device be put in standby. It is HDD's responsibility
            to bring the chip to full power and do a disassoc before calling
            this API.
    \param  hHal - The handle returned by macOpen.
    \param  - callbackRoutine Callback routine invoked in case of success/failure
    \return eHalStatus
      eHAL_STATUS_SUCCESS - device is in Standby mode
      eHAL_STATUS_FAILURE - device cannot be put in standby mode
      eHAL_STATUS_PMC_PENDING - device is being put in standby mode
  ---------------------------------------------------------------------------*/
eHalStatus sme_RequestStandby (
   tHalHandle hHal,
   void (*callbackRoutine) (void *callbackContext, eHalStatus status),
   void *callbackContext)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_REQUEST_STANDBY, NO_SESSION, 0));
   smsLog( pMac, LOG1, FL(" called") );
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcRequestStandby(hHal, callbackRoutine, callbackContext);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RegisterPowerSaveCheck
    \brief  Register a power save check routine that is called whenever
            the device is about to enter one of the power save modes.
    \param  hHal - The handle returned by macOpen.
    \param  checkRoutine -  Power save check routine to be registered
    \return eHalStatus
            eHAL_STATUS_SUCCESS - successfully registered
            eHAL_STATUS_FAILURE - not successfully registered
  ---------------------------------------------------------------------------*/
eHalStatus sme_RegisterPowerSaveCheck (
   tHalHandle hHal,
   tANI_BOOLEAN (*checkRoutine) (void *checkContext), void *checkContext)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcRegisterPowerSaveCheck (hHal, checkRoutine, checkContext);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_Register11dScanDoneCallback
    \brief  Register a routine of type csrScanCompleteCallback which is
            called whenever an 11d scan is done
    \param  hHal - The handle returned by macOpen.
    \param  callback -  11d scan complete routine to be registered
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_Register11dScanDoneCallback (
   tHalHandle hHal,
   csrScanCompleteCallback callback)
{
   eHalStatus status = eHAL_STATUS_SUCCESS;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   pMac->scan.callback11dScanDone = callback;

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_DeregisterPowerSaveCheck
    \brief  Deregister a power save check routine
    \param  hHal - The handle returned by macOpen.
    \param  checkRoutine -  Power save check routine to be deregistered
    \return eHalStatus
            eHAL_STATUS_SUCCESS - successfully deregistered
            eHAL_STATUS_FAILURE - not successfully deregistered
  ---------------------------------------------------------------------------*/
eHalStatus sme_DeregisterPowerSaveCheck (
   tHalHandle hHal,
   tANI_BOOLEAN (*checkRoutine) (void *checkContext))
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcDeregisterPowerSaveCheck (hHal, checkRoutine);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_RegisterDeviceStateUpdateInd
    \brief  Register a callback routine that is called whenever
            the device enters a new device state (Full Power, BMPS, UAPSD)
    \param  hHal - The handle returned by macOpen.
    \param  callbackRoutine -  Callback routine to be registered
    \param  callbackContext -  Cookie to be passed back during callback
    \return eHalStatus
            eHAL_STATUS_SUCCESS - successfully registered
            eHAL_STATUS_FAILURE - not successfully registered
  ---------------------------------------------------------------------------*/
eHalStatus sme_RegisterDeviceStateUpdateInd (
   tHalHandle hHal,
   void (*callbackRoutine) (void *callbackContext, tPmcState pmcState),
   void *callbackContext)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcRegisterDeviceStateUpdateInd (hHal, callbackRoutine, callbackContext);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_DeregisterDeviceStateUpdateInd
    \brief  Deregister a routine that was registered for device state changes
    \param  hHal - The handle returned by macOpen.
    \param  callbackRoutine -  Callback routine to be deregistered
    \return eHalStatus
            eHAL_STATUS_SUCCESS - successfully deregistered
            eHAL_STATUS_FAILURE - not successfully deregistered
  ---------------------------------------------------------------------------*/
eHalStatus sme_DeregisterDeviceStateUpdateInd (
   tHalHandle hHal,
   void (*callbackRoutine) (void *callbackContext, tPmcState pmcState))
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcDeregisterDeviceStateUpdateInd (hHal, callbackRoutine);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_WowlAddBcastPattern
    \brief  Add a pattern for Pattern Byte Matching in Wowl mode. Firmware will
            do a pattern match on these patterns when Wowl is enabled during BMPS
            mode. Note that Firmware performs the pattern matching only on
            broadcast frames and while Libra is in BMPS mode.
    \param  hHal - The handle returned by macOpen.
    \param  pattern -  Pattern to be added
    \return eHalStatus
            eHAL_STATUS_FAILURE  Cannot add pattern
            eHAL_STATUS_SUCCESS  Request accepted.
  ---------------------------------------------------------------------------*/
eHalStatus sme_WowlAddBcastPattern (
   tHalHandle hHal,
   tpSirWowlAddBcastPtrn pattern,
   tANI_U8    sessionId)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_WOWL_ADDBCAST_PATTERN, sessionId, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
       status = pmcWowlAddBcastPattern (hHal, pattern, sessionId);
       sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_WowlDelBcastPattern
    \brief  Delete a pattern that was added for Pattern Byte Matching.
    \param  hHal - The handle returned by macOpen.
    \param  pattern -  Pattern to be deleted
    \return eHalStatus
            eHAL_STATUS_FAILURE  Cannot delete pattern
            eHAL_STATUS_SUCCESS  Request accepted.
  ---------------------------------------------------------------------------*/
eHalStatus sme_WowlDelBcastPattern (
   tHalHandle hHal,
   tpSirWowlDelBcastPtrn pattern,
   tANI_U8  sessionId)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_WOWL_DELBCAST_PATTERN, sessionId, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
       status = pmcWowlDelBcastPattern (hHal, pattern, sessionId);
       sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_EnterWowl
    \brief  This is the SME API exposed to HDD to request enabling of WOWL mode.
            WoWLAN works on top of BMPS mode. If the device is not in BMPS mode,
            SME will will cache the information that WOWL has been enabled and
            attempt to put the device in BMPS. On entry into BMPS, SME will
            enable the WOWL mode.
            Note 1: If we exit BMPS mode (someone requests full power), we
            will NOT resume WOWL when we go back to BMPS again. Request for full
            power (while in WOWL mode) means disable WOWL and go to full power.
            Note 2: Both UAPSD and WOWL work on top of BMPS. On entry into BMPS, SME
            will give priority to UAPSD and enable only UAPSD if both UAPSD and WOWL
            are required. Currently there is no requirement or use case to support
            UAPSD and WOWL at the same time.

    \param  hHal - The handle returned by macOpen.
    \param  enterWowlCallbackRoutine -  Callback routine provided by HDD.
                               Used for success/failure notification by SME
    \param  enterWowlCallbackContext - A cookie passed by HDD, that is passed back to HDD
                              at the time of callback.
    \param  wakeReasonIndCB -  Callback routine provided by HDD.
                               Used for Wake Reason Indication by SME
    \param  wakeReasonIndCBContext - A cookie passed by HDD, that is passed back to HDD
                              at the time of callback.
    \return eHalStatus
            eHAL_STATUS_SUCCESS  Device is already in WoWLAN mode
            eHAL_STATUS_FAILURE  Device cannot enter WoWLAN mode.
            eHAL_STATUS_PMC_PENDING  Request accepted. SME will enable WOWL after
                                      BMPS mode is entered.
  ---------------------------------------------------------------------------*/
eHalStatus sme_EnterWowl (
    tHalHandle hHal,
    void (*enterWowlCallbackRoutine) (void *callbackContext, eHalStatus status),
    void *enterWowlCallbackContext,
#ifdef WLAN_WAKEUP_EVENTS
    void (*wakeIndicationCB) (void *callbackContext, tpSirWakeReasonInd pWakeReasonInd),
    void *wakeIndicationCBContext,
#endif // WLAN_WAKEUP_EVENTS
    tpSirSmeWowlEnterParams wowlEnterParams, tANI_U8 sessionId)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_ENTER_WOWL, sessionId, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
       status = pmcEnterWowl (hHal, enterWowlCallbackRoutine, enterWowlCallbackContext,
#ifdef WLAN_WAKEUP_EVENTS
                              wakeIndicationCB, wakeIndicationCBContext,
#endif // WLAN_WAKEUP_EVENTS
                              wowlEnterParams, sessionId);
       sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}
/* ---------------------------------------------------------------------------
    \fn sme_ExitWowl
    \brief  This is the SME API exposed to HDD to request exit from WoWLAN mode.
            SME will initiate exit from WoWLAN mode and device will be put in BMPS
            mode.
    \param  hHal - The handle returned by macOpen.
    \param  wowlExitParams - Carries info on which smesession
                             wowl exit is requested.
    \return eHalStatus
            eHAL_STATUS_FAILURE  Device cannot exit WoWLAN mode.
            eHAL_STATUS_SUCCESS  Request accepted to exit WoWLAN mode.
  ---------------------------------------------------------------------------*/
eHalStatus sme_ExitWowl (tHalHandle hHal, tpSirSmeWowlExitParams wowlExitParams)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_EXIT_WOWL, NO_SESSION, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
       status = pmcExitWowl (hHal, wowlExitParams);
       sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

/* ---------------------------------------------------------------------------

    \fn sme_RoamSetKey

    \brief To set encryption key. This function should be called only when connected
    This is an asynchronous API.

    \param pSetKeyInfo - pointer to a caller allocated object of tCsrSetContextInfo

    \param pRoamId  Upon success return, this is the id caller can use to identify the request in roamcallback

    \return eHalStatus  SUCCESS  Roam callback will be called indicate actually results

                         FAILURE or RESOURCES  The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamSetKey(tHalHandle hHal, tANI_U8 sessionId, tCsrRoamSetKey *pSetKey, tANI_U32 *pRoamId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tANI_U32 roamId;
   tANI_U32 i;
   tCsrRoamSession *pSession = NULL;

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_SET_KEY, sessionId, 0));
   if (pSetKey->keyLength > CSR_MAX_KEY_LEN)
   {
      smsLog(pMac, LOGE, FL("Invalid key length %d"), pSetKey->keyLength);
      return eHAL_STATUS_FAILURE;
   }
   /*Once Setkey is done, we can go in BMPS*/
   if(pSetKey->keyLength) {
     pMac->pmc.remainInPowerActiveTillDHCP = FALSE;
     smsLog(pMac, LOG1, FL("Reset remainInPowerActiveTillDHCP"
                           " to allow BMPS"));
   }

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      roamId = GET_NEXT_ROAM_ID(&pMac->roam);
      if(pRoamId)
      {
         *pRoamId = roamId;
      }

      smsLog(pMac, LOG2, FL("keyLength %d"), pSetKey->keyLength);

      for(i=0; i<pSetKey->keyLength; i++)
          smsLog(pMac, LOG2, FL("%02x"), pSetKey->Key[i]);

      smsLog(pMac, LOG2, "\n sessionId=%d roamId=%d", sessionId, roamId);

      pSession = CSR_GET_SESSION(pMac, sessionId);

      if(!pSession)
      {
         smsLog(pMac, LOGE, FL("  session %d not found "), sessionId);
         sme_ReleaseGlobalLock( &pMac->sme );
         return eHAL_STATUS_FAILURE;
      }

      if(CSR_IS_INFRA_AP(&pSession->connectedProfile))
      {
         if(pSetKey->keyDirection == eSIR_TX_DEFAULT)
         {
            if ( ( eCSR_ENCRYPT_TYPE_WEP40 == pSetKey->encType ) ||
                 ( eCSR_ENCRYPT_TYPE_WEP40_STATICKEY == pSetKey->encType ))
            {
               pSession->pCurRoamProfile->negotiatedUCEncryptionType = eCSR_ENCRYPT_TYPE_WEP40_STATICKEY;
            }
            if ( ( eCSR_ENCRYPT_TYPE_WEP104 == pSetKey->encType ) ||
                 ( eCSR_ENCRYPT_TYPE_WEP104_STATICKEY == pSetKey->encType ))
            {
               pSession->pCurRoamProfile->negotiatedUCEncryptionType = eCSR_ENCRYPT_TYPE_WEP104_STATICKEY;
            }
         }
      }

      status = csrRoamSetKey ( pMac, sessionId, pSetKey, roamId );
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/*
 * sme_roam_set_default_key_index - function to set default wep key idx
 * @hHal: pointer to hal handler
 * @session_id: session id
 * @default_idx: default wep key index
 *
 * function prepares a message and post to WMA to set wep default
 * key index
 *
 * return: Success:eHAL_STATUS_SUCCESS Failure: Error value
 */
eHalStatus sme_roam_set_default_key_index(tHalHandle hHal, uint8_t session_id,
				 uint8_t default_idx)
{
	vos_msg_t msg;
	struct wep_update_default_key_idx *update_key;

	update_key = vos_mem_malloc(sizeof(struct wep_update_default_key_idx));
	if (!update_key) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  "Failed to allocate memory for update key");
		return eHAL_STATUS_FAILED_ALLOC;
	}

	update_key->session_id = session_id;
	update_key->default_idx = default_idx;

	msg.type = WDA_UPDATE_WEP_DEFAULT_KEY;
	msg.reserved = 0;
	msg.bodyptr = (void *)update_key;

	if (VOS_STATUS_SUCCESS !=
			vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_FATAL,
			  "%s: Failed to post msg to WDA", __func__);
		vos_mem_free(update_key);
		return eHAL_STATUS_FAILURE;
	}

	return eHAL_STATUS_SUCCESS;
}


/* ---------------------------------------------------------------------------

    \fn sme_RoamRemoveKey

    \brief To set encryption key. This is an asynchronous API.

    \param pRemoveKey - pointer to a caller allocated object of tCsrRoamRemoveKey

    \param pRoamId  Upon success return, this is the id caller can use to identify the request in roamcallback

    \return eHalStatus  SUCCESS  Roam callback will be called indicate actually results

                         FAILURE or RESOURCES  The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamRemoveKey(tHalHandle hHal, tANI_U8 sessionId,
                             tCsrRoamRemoveKey *pRemoveKey, tANI_U32 *pRoamId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tANI_U32 roamId;

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
          TRACE_CODE_SME_RX_HDD_REMOVE_KEY, sessionId, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      roamId = GET_NEXT_ROAM_ID(&pMac->roam);
      if(pRoamId)
      {
         *pRoamId = roamId;
      }
      status = csrRoamIssueRemoveKeyCommand( pMac, sessionId, pRemoveKey, roamId );
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_GetRssi
    \brief a wrapper function that client calls to register a callback to get
           RSSI

    \param hHal - HAL handle for device
    \param callback - SME sends back the requested stats using the callback
    \param staId -    The station ID for which the stats is requested for
    \param bssid - The bssid of the connected session
    \param lastRSSI - RSSI value at time of request. In case fw cannot provide
                      RSSI, do not hold up but return this value.
    \param pContext - user context to be passed back along with the callback
    \param pVosContext - vos context
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_GetRssi(tHalHandle hHal,
                             tCsrRssiCallback callback,
                             tANI_U8 staId, tCsrBssid bssId, tANI_S8 lastRSSI,
                             void *pContext, void* pVosContext)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                TRACE_CODE_SME_RX_HDD_GET_RSSI, NO_SESSION,  0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      status = csrGetRssi( pMac, callback,
                           staId, bssId, lastRSSI,
                           pContext, pVosContext);
      sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_GetSnr
    \brief a wrapper function that client calls to register a callback to
           get SNR

    \param callback - SME sends back the requested stats using the callback
    \param staId - The station ID for which the stats is requested for
    \param pContext - user context to be passed back along with the callback
    \param pVosContext - vos context
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_GetSnr(tHalHandle hHal,
                      tCsrSnrCallback callback,
                      tANI_U8 staId, tCsrBssid bssId,
                      void *pContext)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      status = csrGetSnr(pMac, callback,
                          staId, bssId, pContext);
      sme_ReleaseGlobalLock( &pMac->sme );
   }
   return status;
}

#if defined(FEATURE_WLAN_ESE) && defined(FEATURE_WLAN_ESE_UPLOAD)
/* ---------------------------------------------------------------------------
    \fn sme_GetTsmStats
    \brief a wrapper function that client calls to register a callback to
     get TSM Stats
    \param callback - SME sends back the requested stats using the callback
    \param staId - The station ID for which the stats is requested for
    \param pContext - user context to be passed back along with the callback
    \param pVosContext - vos context
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_GetTsmStats(tHalHandle hHal,
                             tCsrTsmStatsCallback callback,
                             tANI_U8 staId, tCsrBssid bssId,
                             void *pContext, void* pVosContext, tANI_U8 tid)
{
   eHalStatus     status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      status = csrGetTsmStats( pMac, callback,
                                 staId, bssId, pContext, pVosContext, tid);
      sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}
#endif

/* ---------------------------------------------------------------------------
    \fn sme_GetStatistics
    \brief a wrapper function that client calls to register a callback to get
    different PHY level statistics from CSR.

    \param requesterId - different client requesting for statistics, HDD, UMA/GAN etc
    \param statsMask - The different category/categories of stats requester is looking for
    \param callback - SME sends back the requested stats using the callback
    \param periodicity - If requester needs periodic update in millisec, 0 means
                         it's an one time request
    \param cache - If requester is happy with cached stats
    \param staId - The station ID for which the stats is requested for
    \param pContext - user context to be passed back along with the callback
    \param sessionId - sme session interface
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_GetStatistics(tHalHandle hHal, eCsrStatsRequesterType requesterId,
                             tANI_U32 statsMask,
                             tCsrStatsCallback callback,
                             tANI_U32 periodicity, tANI_BOOLEAN cache,
                             tANI_U8 staId, void *pContext,
                             tANI_U8 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                TRACE_CODE_SME_RX_HDD_GET_STATS, NO_SESSION,  periodicity));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      status = csrGetStatistics( pMac, requesterId , statsMask, callback,
                                 periodicity, cache, staId, pContext,
                                 sessionId );
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);

}

eHalStatus sme_getLinkStatus(tHalHandle hHal,
                             tCsrLinkStatusCallback callback,
                             void *pContext,
                             tANI_U8 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tAniGetLinkStatus *pMsg;
   vos_msg_t vosMessage;

   status = sme_AcquireGlobalLock(&pMac->sme);
   if (HAL_STATUS_SUCCESS(status)) {
        pMsg = vos_mem_malloc(sizeof(tAniGetLinkStatus));
        if (NULL == pMsg) {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                   "%s: Not able to allocate memory for link status", __func__);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        pMsg->msgType = WDA_LINK_STATUS_GET_REQ;
        pMsg->msgLen = (tANI_U16)sizeof(tAniGetLinkStatus);
        pMsg->sessionId = sessionId;
        pMac->sme.linkStatusContext = pContext;
        pMac->sme.linkStatusCallback = callback;

        vosMessage.type = WDA_LINK_STATUS_GET_REQ;
        vosMessage.bodyptr = pMsg;
        vosMessage.reserved = 0;

        if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA,
                                   &vosMessage))) {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                         "%s: Post LINK STATUS MSG fail", __func__);
           vos_mem_free(pMsg);
           pMac->sme.linkStatusContext = NULL;
           pMac->sme.linkStatusCallback = NULL;
           status = eHAL_STATUS_FAILURE;
        }

        sme_ReleaseGlobalLock(&pMac->sme);
   }

   return (status);
}

/**
 * sme_get_fw_state() - post message to wma to get firmware state
 * @callback: HDD callback to be called on receiving firmware state
 * @pcontext: callback context
 *
 * Return: eHAL_STATUS_SUCCESS on success or failure status
 */
eHalStatus sme_get_fw_state(tHalHandle hHal,
				tcsr_fw_state_callback callback,
				void *context)
{
	eHalStatus status = eHAL_STATUS_FAILURE;
	tpAniSirGlobal mac = PMAC_STRUCT(hHal);
	vos_msg_t vos_message;

	status = sme_AcquireGlobalLock(&mac->sme);
	if (HAL_STATUS_SUCCESS(status)) {
		vos_message.type = WDA_GET_FW_STATUS_REQ;
		vos_message.bodyptr = NULL;
		vos_message.reserved = 0;
		mac->sme.fw_state_context = context;
		mac->sme.fw_state_callback = callback;

		if (!VOS_IS_STATUS_SUCCESS(
			vos_mq_post_message(VOS_MODULE_ID_WDA, &vos_message))) {
			smsLog(mac, LOGE, FL("Post firmware STATUS MSG fail"));
			mac->sme.fw_state_context = NULL;
			mac->sme.fw_state_callback = NULL;
			status = eHAL_STATUS_FAILURE;
		}

		sme_ReleaseGlobalLock(&mac->sme);
	}

	return status;
}

/* ---------------------------------------------------------------------------
    \fn smeGetTLSTAState
    \helper function to get the TL STA State whenever the function is called.

    \param staId - The staID to be passed to the TL
            to get the relevant TL STA State
    \return the state as tANI_U16
  ---------------------------------------------------------------------------*/
tANI_U16 smeGetTLSTAState(tHalHandle hHal, tANI_U8 staId)
{
   tANI_U16 tlSTAState = TL_INIT_STATE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   eHalStatus status = eHAL_STATUS_FAILURE;

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      tlSTAState = csrGetTLSTAState( pMac, staId);
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return tlSTAState;
}

/* ---------------------------------------------------------------------------

    \fn sme_GetCountryCode

    \brief To return the current country code. If no country code is applied,
           default country code is used to fill the buffer.
    If 11d supported is turned off, an error is return and the last
    applied/default country code is used.
    This is a synchronous API.

    \param pBuf - pointer to a caller allocated buffer for returned country code

    \param pbLen  For input, this parameter indicates how big is the buffer.
                  Upon return, this parameter has the number of bytes for
                  country. If pBuf doesn't have enough space,
                  this function returns fail status and this parameter contains
                  the number that is needed.

    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.

  ----------------------------------------------------------------------------*/
eHalStatus sme_GetCountryCode(tHalHandle hHal, tANI_U8 *pBuf, tANI_U8 *pbLen)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                 TRACE_CODE_SME_RX_HDD_GET_CNTRYCODE, NO_SESSION, 0));

    return ( csrGetCountryCode( pMac, pBuf, pbLen ) );
}


/* ---------------------------------------------------------------------------

    \fn sme_SetCountryCode

    \brief To change the current/default country code.
    If 11d supported is turned off, an error is return.
    This is a synchronous API.

    \param pCountry - pointer to a caller allocated buffer for the country code.

    \param pfRestartNeeded  A pointer to caller allocated memory, upon successful return, it indicates
    whether a reset is required.

    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_SetCountryCode(tHalHandle hHal, tANI_U8 *pCountry, tANI_BOOLEAN *pfRestartNeeded)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_SET_CNTRYCODE, NO_SESSION, 0));
    return ( csrSetCountryCode( pMac, pCountry, pfRestartNeeded ) );
}


/* ---------------------------------------------------------------------------
    \fn sme_ResetCountryCodeInformation
    \brief this function is to reset the country code current being used back to EEPROM default
    this includes channel list and power setting. This is a synchronous API.
    \param pfRestartNeeded - pointer to a caller allocated space. Upon successful return, it indicates whether
    a restart is needed to apply the change
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_ResetCountryCodeInformation(tHalHandle hHal, tANI_BOOLEAN *pfRestartNeeded)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    return ( csrResetCountryCodeInformation( pMac, pfRestartNeeded ) );
}


/* ---------------------------------------------------------------------------
    \fn sme_GetSupportedCountryCode
    \brief this function is to get a list of the country code current being supported
    \param pBuf - Caller allocated buffer with at least 3 bytes, upon success return,
    this has the country code list. 3 bytes for each country code. This may be NULL if
    caller wants to know the needed byte count.
    \param pbLen - Caller allocated, as input, it indicates the length of pBuf. Upon success return,
    this contains the length of the data in pBuf. If pbuf is NULL, as input, *pbLen should be 0.
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_GetSupportedCountryCode(tHalHandle hHal, tANI_U8 *pBuf, tANI_U32 *pbLen)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    return ( csrGetSupportedCountryCode( pMac, pBuf, pbLen ) );
}


/* ---------------------------------------------------------------------------
    \fn sme_GetCurrentRegulatoryDomain
    \brief this function is to get the current regulatory domain. This is a synchronous API.
    This function must be called after CFG is downloaded and all the band/mode setting already passed into
    SME. The function fails if 11d support is turned off.
    \param pDomain - Caller allocated buffer to return the current domain.
    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.
  -------------------------------------------------------------------------------*/
eHalStatus sme_GetCurrentRegulatoryDomain(tHalHandle hHal, v_REGDOMAIN_t *pDomain)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status = eHAL_STATUS_INVALID_PARAMETER;

    if( pDomain )
    {
        if( csrIs11dSupported( pMac ) )
        {
            *pDomain = csrGetCurrentRegulatoryDomain( pMac );
            status = eHAL_STATUS_SUCCESS;
        }
        else
        {
            status = eHAL_STATUS_FAILURE;
        }
    }

    return ( status );
}


/* ---------------------------------------------------------------------------
    \fn sme_SetRegulatoryDomain
    \brief this function is to set the current regulatory domain.
    This function must be called after CFG is downloaded and all the band/mode setting already passed into
    SME. This is a synchronous API.
    \param domainId - indicate the domain (defined in the driver) needs to set to.
    See v_REGDOMAIN_t for definition
    \param pfRestartNeeded - pointer to a caller allocated space. Upon successful return, it indicates whether
    a restart is needed to apply the change
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_SetRegulatoryDomain(tHalHandle hHal, v_REGDOMAIN_t domainId, tANI_BOOLEAN *pfRestartNeeded)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    return ( csrSetRegulatoryDomain( pMac, domainId, pfRestartNeeded ) );
}


/* ---------------------------------------------------------------------------

    \fn sme_GetRegulatoryDomainForCountry

    \brief To return a regulatory domain base on a country code. This is a synchronous API.

    \param pCountry - pointer to a caller allocated buffer for input country code.

    \param pDomainId  Upon successful return, it is the domain that country belongs to.
    If it is NULL, returning success means that the country code is known.

    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_GetRegulatoryDomainForCountry(tHalHandle hHal, tANI_U8 *pCountry, v_REGDOMAIN_t *pDomainId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    return csrGetRegulatoryDomainForCountry(pMac, pCountry, pDomainId,
                                            COUNTRY_QUERY);
}




/* ---------------------------------------------------------------------------

    \fn sme_GetSupportedRegulatoryDomains

    \brief To return a list of supported regulatory domains. This is a synchronous API.

    \param pDomains - pointer to a caller allocated buffer for returned regulatory domains.

    \param pNumDomains  For input, this parameter indicates how many
                        domains pDomains can hold. Upon return, this parameter
                        has the number for supported domains. If pDomains
                        doesn't have enough space for all the supported domains,
                        this function returns fail status and this parameter
                        contains the number that is needed.

    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_GetSupportedRegulatoryDomains(tHalHandle hHal, v_REGDOMAIN_t *pDomains, tANI_U32 *pNumDomains)
{
    eHalStatus status = eHAL_STATUS_INVALID_PARAMETER;

    //We support all domains for now
    if( pNumDomains )
    {
        if( NUM_REG_DOMAINS <= *pNumDomains )
        {
            status = eHAL_STATUS_SUCCESS;
        }
        *pNumDomains = NUM_REG_DOMAINS;
    }
    if( HAL_STATUS_SUCCESS( status ) )
    {
        if( pDomains )
        {
            pDomains[0] = REGDOMAIN_FCC;
            pDomains[1] = REGDOMAIN_ETSI;
            pDomains[2] = REGDOMAIN_JAPAN;
            pDomains[3] = REGDOMAIN_WORLD;
            pDomains[4] = REGDOMAIN_N_AMER_EXC_FCC;
            pDomains[5] = REGDOMAIN_APAC;
            pDomains[6] = REGDOMAIN_KOREA;
            pDomains[7] = REGDOMAIN_HI_5GHZ;
            pDomains[8] = REGDOMAIN_NO_5GHZ;
        }
        else
        {
            status = eHAL_STATUS_INVALID_PARAMETER;
        }
    }

    return ( status );
}


//some support functions
tANI_BOOLEAN sme_Is11dSupported(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    return ( csrIs11dSupported( pMac ) );
}


tANI_BOOLEAN sme_Is11hSupported(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    return ( csrIs11hSupported( pMac ) );
}


tANI_BOOLEAN sme_IsWmmSupported(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    return ( csrIsWmmSupported( pMac ) );
}

//Upper layer to get the list of the base channels to scan for passively 11d info from csr
eHalStatus sme_ScanGetBaseChannels( tHalHandle hHal, tCsrChannelInfo * pChannelInfo )
{
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   return(csrScanGetBaseChannels(pMac,pChannelInfo) );
}

/* ---------------------------------------------------------------------------

    \fn sme_ChangeCountryCode

    \brief Change Country code from upper layer during WLAN driver operation.
           This is a synchronous API.

    \param hHal - The handle returned by macOpen.

    \param pCountry New Country Code String

    \param sendRegHint If we want to send reg hint to nl80211

    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_ChangeCountryCode( tHalHandle hHal,
                                          tSmeChangeCountryCallback callback,
                                          tANI_U8 *pCountry,
                                          void *pContext,
                                          void* pVosContext,
                                          tAniBool countryFromUserSpace,
                                          tAniBool sendRegHint )
{
   eHalStatus                status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal            pMac = PMAC_STRUCT( hHal );
   vos_msg_t                 msg;
   tAniChangeCountryCodeReq *pMsg;

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
            TRACE_CODE_SME_RX_HDD_CHANGE_CNTRYCODE, NO_SESSION, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      smsLog(pMac, LOG1, FL(" called"));

      if ((pMac->roam.configParam.Is11dSupportEnabledOriginal == true) &&
          (!pMac->roam.configParam.fSupplicantCountryCodeHasPriority))
      {

          smsLog(pMac, LOGW, "Set Country Code Fail since the STA is associated and userspace does not have priority ");

	  sme_ReleaseGlobalLock( &pMac->sme );
          status = eHAL_STATUS_FAILURE;
          return status;
      }

      pMsg = vos_mem_malloc(sizeof(tAniChangeCountryCodeReq));
      if ( NULL == pMsg )
      {
         smsLog(pMac, LOGE, " csrChangeCountryCode: failed to allocate mem for req");
         sme_ReleaseGlobalLock( &pMac->sme );
         return eHAL_STATUS_FAILURE;
      }

      pMsg->msgType = pal_cpu_to_be16((tANI_U16)eWNI_SME_CHANGE_COUNTRY_CODE);
      pMsg->msgLen = (tANI_U16)sizeof(tAniChangeCountryCodeReq);
      vos_mem_copy(pMsg->countryCode, pCountry, 3);
      pMsg->countryFromUserSpace = countryFromUserSpace;
      pMsg->sendRegHint = sendRegHint;
      pMsg->changeCCCallback = callback;
      pMsg->pDevContext = pContext;
      pMsg->pVosContext = pVosContext;

      msg.type = eWNI_SME_CHANGE_COUNTRY_CODE;
      msg.bodyptr = pMsg;
      msg.reserved = 0;

      if(VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MQ_ID_SME, &msg))
      {
          smsLog(pMac, LOGE, " sme_ChangeCountryCode failed to post msg to self ");
          vos_mem_free((void *)pMsg);
          status = eHAL_STATUS_FAILURE;
      }
      smsLog(pMac, LOG1, FL(" returned"));
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/*--------------------------------------------------------------------------

    \fn sme_GenericChangeCountryCode

    \brief Change Country code from upper layer during WLAN driver operation.
           This is a synchronous API.

    \param hHal - The handle returned by macOpen.

    \param pCountry New Country Code String

    \param reg_domain regulatory domain

    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.

-----------------------------------------------------------------------------*/
eHalStatus sme_GenericChangeCountryCode( tHalHandle hHal,
                                         tANI_U8 *pCountry,
                                         v_REGDOMAIN_t reg_domain)
{
    eHalStatus                status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal            pMac = PMAC_STRUCT( hHal );
    vos_msg_t                 msg;
    tAniGenericChangeCountryCodeReq *pMsg;

    if (NULL == pMac)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_FATAL,
            "%s: pMac is null", __func__);
        return status;
    }

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        smsLog(pMac, LOG1, FL(" called"));
        pMsg = vos_mem_malloc(sizeof(tAniGenericChangeCountryCodeReq));

        if (NULL == pMsg)
        {
            smsLog(pMac, LOGE, " sme_GenericChangeCountryCode: failed to allocate mem for req");
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        pMsg->msgType = pal_cpu_to_be16((tANI_U16)eWNI_SME_GENERIC_CHANGE_COUNTRY_CODE);
        pMsg->msgLen = (tANI_U16)sizeof(tAniGenericChangeCountryCodeReq);
        vos_mem_copy(pMsg->countryCode, pCountry, 2);
        pMsg->countryCode[2] = ' '; /* For ASCII space */
        pMsg->domain_index = reg_domain;

        msg.type = eWNI_SME_GENERIC_CHANGE_COUNTRY_CODE;
        msg.bodyptr = pMsg;
        msg.reserved = 0;

        if (VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MQ_ID_SME, &msg))
        {
            smsLog(pMac, LOGE, "sme_GenericChangeCountryCode failed to post msg to self");
            vos_mem_free(pMsg);
            status = eHAL_STATUS_FAILURE;
        }
        smsLog(pMac, LOG1, FL(" returned"));
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}
/* ---------------------------------------------------------------------------

    \fn sme_DHCPStartInd

    \brief API to signal the FW about the DHCP Start event.

    \param hHal - HAL handle for device.

    \param device_mode - mode(AP,SAP etc) of the device.

    \param macAddr - MAC address of the adapter.

    \param sessionId - session ID.

    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.
  --------------------------------------------------------------------------*/
eHalStatus sme_DHCPStartInd( tHalHandle hHal,
                                   tANI_U8 device_mode,
                                   tANI_U8 *macAddr,
                                   tANI_U8 sessionId )
{
    eHalStatus          status;
    VOS_STATUS          vosStatus;
    tpAniSirGlobal      pMac = PMAC_STRUCT( hHal );
    vos_msg_t           vosMessage;
    tAniDHCPInd         *pMsg;
    tCsrRoamSession     *pSession;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if ( eHAL_STATUS_SUCCESS == status)
    {
        pSession = CSR_GET_SESSION( pMac, sessionId );

        if (!pSession)
        {
            smsLog(pMac, LOGE, FL("session %d not found "), sessionId);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        pMsg = (tAniDHCPInd*)vos_mem_malloc(sizeof(tAniDHCPInd));
        if (NULL == pMsg)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                   "%s: Not able to allocate memory for dhcp start", __func__);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }
        pMsg->msgType = WDA_DHCP_START_IND;
        pMsg->msgLen = (tANI_U16)sizeof(tAniDHCPInd);
        pMsg->device_mode = device_mode;
        vos_mem_copy( pMsg->adapterMacAddr, macAddr, sizeof(tSirMacAddr));
        vos_mem_copy( pMsg->peerMacAddr, pSession->connectedProfile.bssid,
                      sizeof(tSirMacAddr) );

        vosMessage.type = WDA_DHCP_START_IND;
        vosMessage.bodyptr = pMsg;
        vosMessage.reserved = 0;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                  sessionId, vosMessage.type));
        vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) )
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                         "%s: Post DHCP Start MSG fail", __func__);
           vos_mem_free(pMsg);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}
/* ---------------------------------------------------------------------------
    \fn sme_DHCPStopInd

    \brief API to signal the FW about the DHCP complete event.

    \param hHal - HAL handle for device.

    \param device_mode - mode(AP, SAP etc) of the device.

    \param macAddr - MAC address of the adapter.

    \param sessionId - session ID.

    \return eHalStatus  SUCCESS.
                         FAILURE or RESOURCES  The API finished and failed.
  --------------------------------------------------------------------------*/
eHalStatus sme_DHCPStopInd( tHalHandle hHal,
                              tANI_U8 device_mode,
                              tANI_U8 *macAddr,
                              tANI_U8 sessionId )
{
    eHalStatus          status;
    VOS_STATUS          vosStatus;
    tpAniSirGlobal      pMac = PMAC_STRUCT( hHal );
    vos_msg_t           vosMessage;
    tAniDHCPInd         *pMsg;
    tCsrRoamSession     *pSession;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if ( eHAL_STATUS_SUCCESS == status)
    {
        pSession = CSR_GET_SESSION( pMac, sessionId );

        if (!pSession)
        {
            smsLog(pMac, LOGE, FL("session %d not found "), sessionId);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        pMsg = (tAniDHCPInd*)vos_mem_malloc(sizeof(tAniDHCPInd));
        if (NULL == pMsg)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Not able to allocate memory for dhcp stop", __func__);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
       }

       pMsg->msgType = WDA_DHCP_STOP_IND;
       pMsg->msgLen = (tANI_U16)sizeof(tAniDHCPInd);
       pMsg->device_mode = device_mode;
       vos_mem_copy( pMsg->adapterMacAddr, macAddr, sizeof(tSirMacAddr));
       vos_mem_copy( pMsg->peerMacAddr, pSession->connectedProfile.bssid,
                     sizeof(tSirMacAddr) );

       vosMessage.type = WDA_DHCP_STOP_IND;
       vosMessage.bodyptr = pMsg;
       vosMessage.reserved = 0;
       MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 sessionId, vosMessage.type));
       vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
       if ( !VOS_IS_STATUS_SUCCESS(vosStatus) )
       {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                        "%s: Post DHCP Stop MSG fail", __func__);
           vos_mem_free(pMsg);
           status = eHAL_STATUS_FAILURE;
       }

       sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}

/*---------------------------------------------------------------------------

    \fn sme_TXFailMonitorStopInd

    \brief API to signal the FW to start monitoring TX failures

    \return eHalStatus  SUCCESS.

                         FAILURE or RESOURCES  The API finished and failed.
 --------------------------------------------------------------------------*/
eHalStatus sme_TXFailMonitorStartStopInd(tHalHandle hHal, tANI_U8 tx_fail_count,
                                         void * txFailIndCallback)
{
    eHalStatus            status;
    VOS_STATUS            vosStatus;
    tpAniSirGlobal        pMac = PMAC_STRUCT(hHal);
    vos_msg_t             vosMessage;
    tAniTXFailMonitorInd  *pMsg;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if ( eHAL_STATUS_SUCCESS == status)
    {
        pMsg = (tAniTXFailMonitorInd*)
                   vos_mem_malloc(sizeof(tAniTXFailMonitorInd));
        if (NULL == pMsg)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                   "%s: Failed to allocate memory", __func__);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        pMsg->msgType = WDA_TX_FAIL_MONITOR_IND;
        pMsg->msgLen = (tANI_U16)sizeof(tAniTXFailMonitorInd);

        //tx_fail_count = 0 should disable the Monitoring in FW
        pMsg->tx_fail_count = tx_fail_count;
        pMsg->txFailIndCallback = txFailIndCallback;

        vosMessage.type = WDA_TX_FAIL_MONITOR_IND;
        vosMessage.bodyptr = pMsg;
        vosMessage.reserved = 0;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) )
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                         "%s: Post TX Fail monitor Start MSG fail", __func__);
           vos_mem_free(pMsg);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_BtcSignalBtEvent
    \brief  API to signal Bluetooth (BT) event to the WLAN driver. Based on the
            BT event type and the current operating mode of Libra (full power,
            BMPS, UAPSD etc), appropriate Bluetooth Coexistence (BTC) strategy
            would be employed.
    \param  hHal - The handle returned by macOpen.
    \param  pBtEvent -  Pointer to a caller allocated object of type tSmeBtEvent
                        Caller owns the memory and is responsible for freeing it.
    \return VOS_STATUS
            VOS_STATUS_E_FAILURE  BT Event not passed to HAL. This can happen
                                   if BTC execution mode is set to BTC_WLAN_ONLY
                                   or BTC_PTA_ONLY.
            VOS_STATUS_SUCCESS    BT Event passed to HAL
  ---------------------------------------------------------------------------*/
VOS_STATUS sme_BtcSignalBtEvent (tHalHandle hHal, tpSmeBtEvent pBtEvent)
{
    VOS_STATUS status = VOS_STATUS_E_FAILURE;

#ifndef WLAN_MDM_CODE_REDUCTION_OPT
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
           TRACE_CODE_SME_RX_HDD_BTC_SIGNALEVENT, NO_SESSION, 0));
    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        status = btcSignalBTEvent (hHal, pBtEvent);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#endif
    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_BtcSetConfig
    \brief  API to change the current Bluetooth Coexistence (BTC) configuration
            This function should be invoked only after CFG download has completed.
            Calling it after sme_HDDReadyInd is recommended.
    \param  hHal - The handle returned by macOpen.
    \param  pSmeBtcConfig - Pointer to a caller allocated object of type tSmeBtcConfig.
                            Caller owns the memory and is responsible for freeing it.
    \return VOS_STATUS
            VOS_STATUS_E_FAILURE  Config not passed to HAL.
            VOS_STATUS_SUCCESS  Config passed to HAL
  ---------------------------------------------------------------------------*/
VOS_STATUS sme_BtcSetConfig (tHalHandle hHal, tpSmeBtcConfig pSmeBtcConfig)
{
    VOS_STATUS status = VOS_STATUS_E_FAILURE;
#ifndef WLAN_MDM_CODE_REDUCTION_OPT
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                 TRACE_CODE_SME_RX_HDD_BTC_SETCONFIG, NO_SESSION, 0));
    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        status = btcSetConfig (hHal, pSmeBtcConfig);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#endif
    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_BtcGetConfig
    \brief  API to retrieve the current Bluetooth Coexistence (BTC) configuration
    \param  hHal - The handle returned by macOpen.
    \param  pSmeBtcConfig - Pointer to a caller allocated object of type
                            tSmeBtcConfig. Caller owns the memory and is responsible
                            for freeing it.
    \return VOS_STATUS
            VOS_STATUS_E_FAILURE - failure
            VOS_STATUS_SUCCESS  success
  ---------------------------------------------------------------------------*/
VOS_STATUS sme_BtcGetConfig (tHalHandle hHal, tpSmeBtcConfig pSmeBtcConfig)
{
    VOS_STATUS status = VOS_STATUS_E_FAILURE;
#ifndef WLAN_MDM_CODE_REDUCTION_OPT
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_BTC_GETCONFIG, NO_SESSION, 0));
    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        status = btcGetConfig (hHal, pSmeBtcConfig);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#endif
    return (status);
}
/* ---------------------------------------------------------------------------
    \fn sme_SetCfgPrivacy
    \brief  API to set configure privacy parameters
    \param  hHal - The handle returned by macOpen.
    \param  pProfile - Pointer CSR Roam profile.
    \param  fPrivacy - This parameter indicates status of privacy

    \return void
  ---------------------------------------------------------------------------*/
void sme_SetCfgPrivacy( tHalHandle hHal,
                        tCsrRoamProfile *pProfile,
                        tANI_BOOLEAN fPrivacy
                        )
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
               TRACE_CODE_SME_RX_HDD_SET_CFGPRIVACY, NO_SESSION, 0));
    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        csrSetCfgPrivacy(pMac, pProfile, fPrivacy);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
}

#if defined WLAN_FEATURE_VOWIFI
/* ---------------------------------------------------------------------------
    \fn sme_NeighborReportRequest
    \brief  API to request neighbor report.
    \param  hHal - The handle returned by macOpen.
    \param  pRrmNeighborReq - Pointer to a caller allocated object of type
                            tRrmNeighborReq. Caller owns the memory and is responsible
                            for freeing it.
    \return VOS_STATUS
            VOS_STATUS_E_FAILURE - failure
            VOS_STATUS_SUCCESS  success
  ---------------------------------------------------------------------------*/
VOS_STATUS sme_NeighborReportRequest (tHalHandle hHal, tANI_U8 sessionId,
                                    tpRrmNeighborReq pRrmNeighborReq, tpRrmNeighborRspCallbackInfo callbackInfo)
{
    VOS_STATUS status = VOS_STATUS_E_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                 TRACE_CODE_SME_RX_HDD_NEIGHBOR_REPORTREQ, NO_SESSION, 0));

    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        status = sme_RrmNeighborReportRequest (hHal, sessionId, pRrmNeighborReq, callbackInfo);
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}
#endif

void pmcLog(tpAniSirGlobal pMac, tANI_U32 loglevel, const char *pString, ...)
{
    VOS_TRACE_LEVEL  vosDebugLevel;
    char    logBuffer[LOG_SIZE];
    va_list marker;

    /* getting proper Debug level */
    vosDebugLevel = getVosDebugLevel(loglevel);

    /* extracting arguments from pstring */
    va_start( marker, pString );
    vsnprintf(logBuffer, LOG_SIZE, pString, marker);

    VOS_TRACE(VOS_MODULE_ID_PMC, vosDebugLevel, "%s", logBuffer);
    va_end( marker );
}


void smsLog(tpAniSirGlobal pMac, tANI_U32 loglevel, const char *pString,...)
{
#ifdef WLAN_DEBUG
    // Verify against current log level
    if ( loglevel > pMac->utils.gLogDbgLevel[LOG_INDEX_FOR_MODULE( SIR_SMS_MODULE_ID )] )
        return;
    else
    {
        va_list marker;

        va_start( marker, pString );     /* Initialize variable arguments. */

        logDebug(pMac, SIR_SMS_MODULE_ID, loglevel, pString, marker);

        va_end( marker );              /* Reset variable arguments.      */
    }
#endif
}

/* ---------------------------------------------------------------------------
    \fn sme_GetWcnssWlanCompiledVersion
    \brief  This API returns the version of the WCNSS WLAN API with
            which the HOST driver was built
    \param  hHal - The handle returned by macOpen.
    \param  pVersion - Points to the Version structure to be filled
    \return VOS_STATUS
            VOS_STATUS_E_INVAL - failure
            VOS_STATUS_SUCCESS  success
  ---------------------------------------------------------------------------*/
VOS_STATUS sme_GetWcnssWlanCompiledVersion(tHalHandle hHal,
                                           tSirVersionType *pVersion)
{
    VOS_STATUS status = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    v_CONTEXT_t vosContext = vos_get_global_context(VOS_MODULE_ID_SME, NULL);

    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        if( pVersion != NULL )
        {
            status = WDA_GetWcnssWlanCompiledVersion(vosContext, pVersion);
        }
        else
        {
            status = VOS_STATUS_E_INVAL;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}


/* ---------------------------------------------------------------------------
    \fn sme_GetWcnssWlanReportedVersion
    \brief  This API returns the version of the WCNSS WLAN API with
            which the WCNSS driver reports it was built
    \param  hHal - The handle returned by macOpen.
    \param  pVersion - Points to the Version structure to be filled
    \return VOS_STATUS
            VOS_STATUS_E_INVAL - failure
            VOS_STATUS_SUCCESS  success
  ---------------------------------------------------------------------------*/
VOS_STATUS sme_GetWcnssWlanReportedVersion(tHalHandle hHal,
                                           tSirVersionType *pVersion)
{
    VOS_STATUS status = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    v_CONTEXT_t vosContext = vos_get_global_context(VOS_MODULE_ID_SME, NULL);

    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        if( pVersion != NULL )
        {
            status = WDA_GetWcnssWlanReportedVersion(vosContext, pVersion);
        }
        else
        {
            status = VOS_STATUS_E_INVAL;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}


/* ---------------------------------------------------------------------------
    \fn sme_GetWcnssSoftwareVersion
    \brief  This API returns the version string of the WCNSS driver
    \param  hHal - The handle returned by macOpen.
    \param  pVersion - Points to the Version string buffer to be filled
    \param  versionBufferSize - THe size of the Version string buffer
    \return VOS_STATUS
            VOS_STATUS_E_INVAL - failure
            VOS_STATUS_SUCCESS  success
  ---------------------------------------------------------------------------*/
VOS_STATUS sme_GetWcnssSoftwareVersion(tHalHandle hHal,
                                       tANI_U8 *pVersion,
                                       tANI_U32 versionBufferSize)
{
    VOS_STATUS status = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    v_CONTEXT_t vosContext = vos_get_global_context(VOS_MODULE_ID_SME, NULL);

    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        if( pVersion != NULL )
        {
            status = WDA_GetWcnssSoftwareVersion(vosContext, pVersion,
                                                 versionBufferSize);
        }
        else
        {
            status = VOS_STATUS_E_INVAL;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}


/* ---------------------------------------------------------------------------
    \fn sme_GetWcnssHardwareVersion
    \brief  This API returns the version string of the WCNSS hardware
    \param  hHal - The handle returned by macOpen.
    \param  pVersion - Points to the Version string buffer to be filled
    \param  versionBufferSize - THe size of the Version string buffer
    \return VOS_STATUS
            VOS_STATUS_E_INVAL - failure
            VOS_STATUS_SUCCESS  success
  ---------------------------------------------------------------------------*/
VOS_STATUS sme_GetWcnssHardwareVersion(tHalHandle hHal,
                                       tANI_U8 *pVersion,
                                       tANI_U32 versionBufferSize)
{
    VOS_STATUS status = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    v_CONTEXT_t vosContext = vos_get_global_context(VOS_MODULE_ID_SME, NULL);

    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        if( pVersion != NULL )
        {
            status = WDA_GetWcnssHardwareVersion(vosContext, pVersion,
                                                 versionBufferSize);
        }
        else
        {
            status = VOS_STATUS_E_INVAL;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}


#ifdef FEATURE_WLAN_WAPI

/* ---------------------------------------------------------------------------
    \fn sme_ScanGetBKIDCandidateList
    \brief a wrapper function to return the BKID candidate list
    \param pBkidList - caller allocated buffer point to an array of
                        tBkidCandidateInfo
    \param pNumItems - pointer to a variable that has the number of
                       tBkidCandidateInfo allocated when returning, this is
                       either the number needed or number of items put into
                       pPmkidList
    \return eHalStatus - when fail, it usually means the buffer allocated is not
                         big enough and pNumItems
    has the number of tBkidCandidateInfo.
    \Note: pNumItems is a number of tBkidCandidateInfo,
           not sizeof(tBkidCandidateInfo) * something
  ---------------------------------------------------------------------------*/
eHalStatus sme_ScanGetBKIDCandidateList(tHalHandle hHal, tANI_U32 sessionId,
                                        tBkidCandidateInfo *pBkidList,
                                        tANI_U32 *pNumItems )
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrScanGetBKIDCandidateList( pMac, sessionId, pBkidList, pNumItems );
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}
#endif /* FEATURE_WLAN_WAPI */

#ifdef FEATURE_OEM_DATA_SUPPORT

/*****************************************************************************
 OEM DATA related modifications and function additions
 *****************************************************************************/

/* ---------------------------------------------------------------------------
    \fn sme_OemDataReq
    \brief a wrapper function for OEM DATA REQ
    \param sessionId - session id to be used.
    \param pOemDataReqId - pointer to an object to get back the request ID
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_OemDataReq(tHalHandle hHal,
        tANI_U8 sessionId,
        tOemDataReqConfig *pOemDataReqConfig,
        tANI_U32 *pOemDataReqID)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    do
    {
        //acquire the lock for the sme object
        status = sme_AcquireGlobalLock(&pMac->sme);
        if(HAL_STATUS_SUCCESS(status))
        {
            tANI_U32 lOemDataReqId = pMac->oemData.oemDataReqID++; //let it wrap around

            if(pOemDataReqID)
            {
               *pOemDataReqID = lOemDataReqId;
            }
            else
            {
                sme_ReleaseGlobalLock( &pMac->sme );
                return eHAL_STATUS_FAILURE;
            }

            status = oemData_OemDataReq(hHal, sessionId, pOemDataReqConfig, pOemDataReqID);

            //release the lock for the sme object
            sme_ReleaseGlobalLock( &pMac->sme );
        }
    } while(0);

    smsLog(pMac, LOGW, "exiting function %s", __func__);

    return(status);
}

#endif /*FEATURE_OEM_DATA_SUPPORT*/

/**
 * sme_create_mon_session() - post message to create PE session for monitormode
 * operation
 * @hal_handle: Handle to the HAL
 * @bssid: pointer to bssid
 *
 * Return: eHAL_STATUS_SUCCESS on success, non-zero error code on failure.
 */
eHalStatus sme_create_mon_session(tHalHandle hal_handle, tSirMacAddr bss_id)
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	struct sir_create_session *msg;
	tpAniSirGlobal mac_ptr = PMAC_STRUCT(hal_handle);
	uint16_t msg_len;

	msg_len = (tANI_U16)(sizeof(struct sir_create_session));
	msg = vos_mem_malloc(msg_len);
	if ( NULL != msg )
	{
		vos_mem_set(msg, msg_len, 0);
		msg->type =
			pal_cpu_to_be16((tANI_U16)eWNI_SME_MON_INIT_SESSION);
		msg->msg_len = pal_cpu_to_be16(msg_len);
		vos_mem_copy(msg->bss_id, bss_id, sizeof(tSirMacAddr));
		status = palSendMBMessage(mac_ptr->hHdd, msg);
	}
	return status;
}

/*--------------------------------------------------------------------------

  \brief sme_OpenSession() - Open a session for scan/roam operation.

  This is a synchronous API.


  \param hHal - The handle returned by macOpen.
  \param callback - A pointer to the function caller specifies for roam/connect status indication
  \param pContext - The context passed with callback
  \param pSelfMacAddr - Caller allocated memory filled with self MAC address (6 bytes)
  \param pbSessionId - pointer to a caller allocated buffer for returned session ID

  \return eHAL_STATUS_SUCCESS - session is opened. sessionId returned.

          Other status means SME is failed to open the session.
          eHAL_STATUS_RESOURCES - no more session available.
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_OpenSession(tHalHandle hHal, csrRoamCompleteCallback callback,
                           void *pContext,
                           tANI_U8 *pSelfMacAddr, tANI_U8 *pbSessionId,
                           tANI_U32 type, tANI_U32 subType)
{
   eHalStatus status;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO_HIGH, "%s: type=%d, subType=%d", __func__, type, subType);

   if( NULL == pbSessionId )
   {
      status = eHAL_STATUS_INVALID_PARAMETER;
   }
   else
   {
      status = sme_AcquireGlobalLock( &pMac->sme );
      if ( HAL_STATUS_SUCCESS( status ) )
      {
         status = csrRoamOpenSession( pMac, callback, pContext, pSelfMacAddr,
                                      pbSessionId, type, subType );

         sme_ReleaseGlobalLock( &pMac->sme );
      }
   }
   if( NULL != pbSessionId )
   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                 TRACE_CODE_SME_RX_HDD_OPEN_SESSION,*pbSessionId, 0));

   return ( status );
}


/*--------------------------------------------------------------------------

  \brief sme_CloseSession() - Open a session for scan/roam operation.

  This is a synchronous API.


  \param hHal - The handle returned by macOpen.

  \param sessionId - A previous opened session's ID.

  \return eHAL_STATUS_SUCCESS - session is closed.

          Other status means SME is failed to open the session.
          eHAL_STATUS_INVALID_PARAMETER - session is not opened.
  \sa

  --------------------------------------------------------------------------*/
eHalStatus sme_CloseSession(tHalHandle hHal, tANI_U8 sessionId,
                          csrRoamSessionCloseCallback callback, void *pContext)
{
   eHalStatus status;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   MTRACE(vos_trace(VOS_MODULE_ID_SME,
                 TRACE_CODE_SME_RX_HDD_CLOSE_SESSION, sessionId, 0));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      status = csrRoamCloseSession( pMac, sessionId, FALSE,
                                    callback, pContext );

      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return ( status );
}

/* ---------------------------------------------------------------------------

    \fn sme_RoamUpdateAPWPSIE

    \brief To update AP's WPS IE. This function should be called after SME AP session is created
    This is an asynchronous API.

    \param pAPWPSIES - pointer to a caller allocated object of tSirAPWPSIEs

    \return eHalStatus  SUCCESS 

                         FAILURE or RESOURCES  The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamUpdateAPWPSIE(tHalHandle hHal, tANI_U8 sessionId, tSirAPWPSIEs *pAPWPSIES)
{

   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {

      status = csrRoamUpdateAPWPSIE( pMac, sessionId, pAPWPSIES );

      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}
/* ---------------------------------------------------------------------------

    \fn sme_RoamUpdateAPWPARSNIEs

    \brief To update AP's WPA/RSN IEs. This function should be called after SME AP session is created
    This is an asynchronous API.

    \param pAPSirRSNie - pointer to a caller allocated object of tSirRSNie with WPS/RSN IEs

    \return eHalStatus  SUCCESS 

                         FAILURE or RESOURCES  The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_RoamUpdateAPWPARSNIEs(tHalHandle hHal, tANI_U8 sessionId, tSirRSNie * pAPSirRSNie)
{

   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {

      status = csrRoamUpdateWPARSNIEs( pMac, sessionId, pAPSirRSNie);

      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}
/* ---------------------------------------------------------------------------

    \fn sme_ChangeMCCBeaconInterval

    \brief To update P2P-GO beaconInterval. This function should be called after
    disassociating all the station is done
    This is an asynchronous API.

    \param

    \return eHalStatus  SUCCESS
                        FAILURE or RESOURCES
                        The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_ChangeMCCBeaconInterval(tHalHandle hHal, tANI_U8 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   smsLog(pMac, LOG1, FL("Update Beacon PARAMS "));
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      status = csrSendChngMCCBeaconInterval( pMac, sessionId);
      sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}


/* ---------------------------------------------------------------------------
    \fn     smeIssueFastRoamNeighborAPEvent
    \brief  API to trigger fast BSS roam independent of RSSI triggers
    \param  hHal - The handle returned by macOpen.
    \param  bssid -  Pointer to the BSSID to roam to.
    \param  fastRoamTrig - Trigger to Scan or roam
    \param  sessionId - Session Identifier
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus smeIssueFastRoamNeighborAPEvent(tHalHandle hHal,
                                           tANI_U8 *bssid,
                                           tSmeFastRoamTrigger fastRoamTrig,
                                           tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    tpCsrNeighborRoamControlInfo  pNeighborRoamInfo =
                                &pMac->roam.neighborRoamInfo[sessionId];
    VOS_STATUS  vosStatus = VOS_STATUS_SUCCESS;
    eHalStatus  status    = eHAL_STATUS_SUCCESS;
    tFTRoamCallbackUsrCtx  *pUsrCtx;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                  "%s: invoked", __func__);

        if (eSME_ROAM_TRIGGER_SCAN == fastRoamTrig)
        {
            smsLog(pMac, LOG1, FL("CFG Channel list scan... "));
            pNeighborRoamInfo->cfgRoamEn = eSME_ROAM_TRIGGER_SCAN;
            vos_mem_copy((void *)(&pNeighborRoamInfo->cfgRoambssId),
                       (void *)bssid, sizeof(tSirMacAddr));
            smsLog(pMac, LOG1, "Calling Roam Look Up down Event BSSID "
                   MAC_ADDRESS_STR, MAC_ADDR_ARRAY(pNeighborRoamInfo->cfgRoambssId));

            vosStatus = csrNeighborRoamTransitToCFGChanScan(pMac, sessionId);
            if (VOS_STATUS_SUCCESS != vosStatus)
            {
                smsLog(pMac, LOGE,
                       FL("CFG Channel list scan state failed with status %d "),
                       vosStatus);
            }
        }
        else if (eSME_ROAM_TRIGGER_FAST_ROAM == fastRoamTrig)
        {
             vos_mem_copy((void *)(&pNeighborRoamInfo->cfgRoambssId),
                       (void *)bssid, sizeof(tSirMacAddr));
             pNeighborRoamInfo->cfgRoamEn = eSME_ROAM_TRIGGER_FAST_ROAM;
             smsLog(pMac, LOG1, "Roam to BSSID "MAC_ADDRESS_STR,
                    MAC_ADDR_ARRAY(pNeighborRoamInfo->cfgRoambssId));

             pUsrCtx = vos_mem_malloc(sizeof(*pUsrCtx));
             if (NULL == pUsrCtx) {
                 smsLog(pMac, LOGE, FL("Memory allocation failed"));
                 return eHAL_STATUS_FAILED_ALLOC;
             }

             /* Populate user context */
             pUsrCtx->pMac = pMac;
             pUsrCtx->sessionId = sessionId;

             vosStatus = csrNeighborRoamReassocIndCallback(
                                                         pMac->roam.gVosContext,
                                                         0, pUsrCtx, 0);

             if (!VOS_IS_STATUS_SUCCESS(vosStatus))
             {
                 smsLog(pMac,
                        LOGE,
                        FL(" Call to csrNeighborRoamReassocIndCallback failed, status = %d"),
                        vosStatus);
             }
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return vosStatus;
}
/* ---------------------------------------------------------------------------
    \fn sme_SetHostOffload
    \brief  API to set the host offload feature.
    \param  hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \param  pRequest -  Pointer to the offload request.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_SetHostOffload (tHalHandle hHal, tANI_U8 sessionId,
                                    tpSirHostOffloadReq pRequest)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status = eHAL_STATUS_FAILURE;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
               TRACE_CODE_SME_RX_HDD_SET_HOSTOFFLOAD, sessionId, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
#ifdef WLAN_NS_OFFLOAD
        if(SIR_IPV6_NS_OFFLOAD == pRequest->offloadType)
        {
            status = pmcSetNSOffload( hHal, pRequest, sessionId);
        }
        else
#endif //WLAN_NS_OFFLOAD
        {
            status = pmcSetHostOffload (hHal, pRequest, sessionId);
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

#ifdef WLAN_FEATURE_GTK_OFFLOAD
/* ---------------------------------------------------------------------------
    \fn sme_SetGTKOffload
    \brief  API to set GTK offload information.
    \param  hHal - The handle returned by macOpen.
    \param  pRequest -  Pointer to the GTK offload request.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_SetGTKOffload (tHalHandle hHal, tpSirGtkOffloadParams pRequest,
                                    tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                    TRACE_CODE_SME_RX_HDD_SET_GTKOFFLOAD, sessionId, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        status = pmcSetGTKOffload( hHal, pRequest, sessionId );
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_GetGTKOffload
    \brief  API to get GTK offload information.
    \param  hHal - The handle returned by macOpen.
    \param  pRequest -  Pointer to the GTK offload response.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_GetGTKOffload (tHalHandle hHal, GTKOffloadGetInfoCallback callbackRoutine,
                                    void *callbackContext, tANI_U8 sessionId )
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                TRACE_CODE_SME_RX_HDD_GET_GTKOFFLOAD, sessionId, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        pmcGetGTKOffload(hHal, callbackRoutine, callbackContext, sessionId);
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}
#endif // WLAN_FEATURE_GTK_OFFLOAD

/* ---------------------------------------------------------------------------
    \fn sme_SetKeepAlive
    \brief  API to set the Keep Alive feature.
    \param  hHal - The handle returned by macOpen.
    \param  pRequest -  Pointer to the Keep Alive request.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_SetKeepAlive (tHalHandle hHal, tANI_U8 sessionId,
                                 tpSirKeepAliveReq pRequest)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status;
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        status = pmcSetKeepAlive (hHal, pRequest, sessionId);
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

#ifdef FEATURE_WLAN_SCAN_PNO
/* ---------------------------------------------------------------------------
    \fn sme_SetPreferredNetworkList
    \brief  API to set the Preferred Network List Offload feature.
    \param  hHal - The handle returned by macOpen.
    \param  pRequest -  Pointer to the offload request.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_SetPreferredNetworkList (tHalHandle hHal, tpSirPNOScanReq pRequest, tANI_U8 sessionId, void (*callbackRoutine) (void *callbackContext, tSirPrefNetworkFoundInd *pPrefNetworkFoundInd), void *callbackContext )
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status;

    MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_HDD_PREF_NET_LIST,
                         sessionId, pRequest->ucNetworksCount));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        pmcSetPreferredNetworkList(hHal, pRequest, sessionId, callbackRoutine, callbackContext);
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}
#endif // FEATURE_WLAN_SCAN_PNO

eHalStatus sme_SetPowerParams(tHalHandle hHal, tSirSetPowerParamsReq* pwParams, tANI_BOOLEAN forced)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                       TRACE_CODE_SME_RX_HDD_SET_POWERPARAMS, NO_SESSION, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        pmcSetPowerParams(hHal, pwParams, forced);
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_AbortMacScan
    \brief  API to cancel MAC scan.
    \param  hHal - The handle returned by macOpen.
    \param  sessionId - sessionId on which we need to abort scan.
    \param  reason - Reason to abort the scan.
    \return VOS_STATUS
            VOS_STATUS_E_FAILURE - failure
            VOS_STATUS_SUCCESS  success
  ---------------------------------------------------------------------------*/
eHalStatus sme_AbortMacScan(tHalHandle hHal, tANI_U8 sessionId,
                            eCsrAbortReason reason)
{
    eHalStatus status;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
               TRACE_CODE_SME_RX_HDD_ABORT_MACSCAN, NO_SESSION, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
       status = csrScanAbortMacScan(pMac, sessionId, reason);

       sme_ReleaseGlobalLock( &pMac->sme );
    }

    return ( status );
}

/* ----------------------------------------------------------------------------
        \fn sme_GetOperationChannel
        \brief API to get current channel on which STA is parked
        this function gives channel information only of infra station or IBSS station
        \param hHal, pointer to memory location and sessionId
        \returns eHAL_STATUS_SUCCESS
                eHAL_STATUS_FAILURE
-------------------------------------------------------------------------------*/
eHalStatus sme_GetOperationChannel(tHalHandle hHal, tANI_U32 *pChannel, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    tCsrRoamSession *pSession;

    if (CSR_IS_SESSION_VALID( pMac, sessionId ))
    {
       pSession = CSR_GET_SESSION( pMac, sessionId );

       if(( pSession->connectedProfile.BSSType == eCSR_BSS_TYPE_INFRASTRUCTURE ) ||
          ( pSession->connectedProfile.BSSType == eCSR_BSS_TYPE_IBSS ) ||
          ( pSession->connectedProfile.BSSType == eCSR_BSS_TYPE_INFRA_AP ) ||
          ( pSession->connectedProfile.BSSType == eCSR_BSS_TYPE_START_IBSS ))
       {
           *pChannel =pSession->connectedProfile.operationChannel;
           return eHAL_STATUS_SUCCESS;
       }
    }
    return eHAL_STATUS_FAILURE;
}// sme_GetOperationChannel ends here

/**
 * sme_register_mgmt_frame_ind_callback() - Register a callback for
 * management frame indication to PE.
 *
 * @hal: hal pointer
 * @callback: callback pointer to be registered
 *
 * This function is used to register a callback for management
 * frame indication to PE.
 *
 * Return: Success if msg is posted to PE else Failure.
 */
eHalStatus sme_register_mgmt_frame_ind_callback(tHalHandle hal,
				sir_mgmt_frame_ind_callback callback)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	struct sir_sme_mgmt_frame_cb_req *msg;
	eHalStatus status = eHAL_STATUS_SUCCESS;

	smsLog(mac_ctx, LOG1, FL(": ENTER"));

	if (eHAL_STATUS_SUCCESS ==
			sme_AcquireGlobalLock(&mac_ctx->sme)) {
		msg = vos_mem_malloc(sizeof(*msg));
		if (NULL == msg) {
			smsLog(mac_ctx, LOGE,
				FL("Not able to allocate memory for eWNI_SME_REGISTER_MGMT_FRAME_CB"));
			sme_ReleaseGlobalLock(&mac_ctx->sme);
			return eHAL_STATUS_FAILURE;
		}
		vos_mem_set(msg, sizeof(*msg), 0);
		msg->message_type = eWNI_SME_REGISTER_MGMT_FRAME_CB;
		msg->length          = sizeof(*msg);

		msg->callback = callback;
		status = palSendMBMessage(mac_ctx->hHdd, msg);
		sme_ReleaseGlobalLock(&mac_ctx->sme);
		return status;
	}
	return eHAL_STATUS_FAILURE;
}

/* ---------------------------------------------------------------------------

    \fn sme_RegisterMgtFrame

    \brief To register management frame of specified type and subtype.
    \param frameType - type of the frame that needs to be passed to HDD.
    \param matchData - data which needs to be matched before passing frame
                       to HDD.
    \param matchDataLen - Length of matched data.
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_RegisterMgmtFrame(tHalHandle hHal, tANI_U8 sessionId,
                     tANI_U16 frameType, tANI_U8* matchData, tANI_U16 matchLen)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                          TRACE_CODE_SME_RX_HDD_REGISTER_MGMTFR, sessionId, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        tSirRegisterMgmtFrame *pMsg;
        tANI_U16 len;
        tCsrRoamSession *pSession = CSR_GET_SESSION( pMac, sessionId );

        if(!CSR_IS_SESSION_ANY(sessionId) && !pSession)
        {
            smsLog(pMac, LOGE, FL("  session %d not found "), sessionId);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        if( !CSR_IS_SESSION_ANY(sessionId) && !pSession->sessionActive )
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                        "%s Invalid Sessionid", __func__);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        len = sizeof(tSirRegisterMgmtFrame) + matchLen;

        pMsg = vos_mem_malloc(len);
        if ( NULL == pMsg )
           status = eHAL_STATUS_FAILURE;
        else
        {
            vos_mem_set(pMsg, len, 0);
            pMsg->messageType     = eWNI_SME_REGISTER_MGMT_FRAME_REQ;
            pMsg->length          = len;
            pMsg->sessionId       = sessionId;
            pMsg->registerFrame   = VOS_TRUE;
            pMsg->frameType       = frameType;
            pMsg->matchLen        = matchLen;
            vos_mem_copy(pMsg->matchData, matchData, matchLen);
            status = palSendMBMessage(pMac->hHdd, pMsg);
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return status;
}

/* ---------------------------------------------------------------------------

    \fn sme_DeregisterMgtFrame

    \brief To De-register management frame of specified type and subtype.
    \param frameType - type of the frame that needs to be passed to HDD.
    \param matchData - data which needs to be matched before passing frame
                       to HDD.
    \param matchDataLen - Length of matched data.
    \return eHalStatus
  -------------------------------------------------------------------------------*/
eHalStatus sme_DeregisterMgmtFrame(tHalHandle hHal, tANI_U8 sessionId,
                     tANI_U16 frameType, tANI_U8* matchData, tANI_U16 matchLen)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_DEREGISTER_MGMTFR, sessionId, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        tSirRegisterMgmtFrame *pMsg;
        tANI_U16 len;
        tCsrRoamSession *pSession = CSR_GET_SESSION( pMac, sessionId );

        if(!CSR_IS_SESSION_ANY(sessionId) && !pSession)
        {
            smsLog(pMac, LOGE, FL("  session %d not found "), sessionId);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        if( !CSR_IS_SESSION_ANY(sessionId) && !pSession->sessionActive )
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                        "%s Invalid Sessionid", __func__);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        len = sizeof(tSirRegisterMgmtFrame) + matchLen;

        pMsg = vos_mem_malloc(len);
        if ( NULL == pMsg )
           status = eHAL_STATUS_FAILURE;
        else
        {
            vos_mem_set(pMsg, len, 0);
            pMsg->messageType     = eWNI_SME_REGISTER_MGMT_FRAME_REQ;
            pMsg->length          = len;
            pMsg->registerFrame   = VOS_FALSE;
            pMsg->frameType       = frameType;
            pMsg->matchLen        = matchLen;
            vos_mem_copy(pMsg->matchData, matchData, matchLen);
            status = palSendMBMessage(pMac->hHdd, pMsg);
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_RemainOnChannel
    \brief  API to request remain on channel for 'x' duration. used in p2p in listen state
    \param  hHal - The handle returned by macOpen.
    \param  pRequest -  channel
    \param  duration - duration in ms
    \param callback - HDD registered callback to process reaminOnChannelRsp
    \param context - HDD Callback param
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_RemainOnChannel(tHalHandle hHal, tANI_U8 sessionId,
                               tANI_U8 channel, tANI_U32 duration,
                               remainOnChanCallback callback,
                               void *pContext,
                               tANI_U8 isP2PProbeReqAllowed)
{
  eHalStatus status = eHAL_STATUS_SUCCESS;
  tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

  MTRACE(vos_trace(VOS_MODULE_ID_SME,
            TRACE_CODE_SME_RX_HDD_REMAIN_ONCHAN, sessionId, 0));
  if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
  {
    status = p2pRemainOnChannel (hHal, sessionId, channel, duration, callback, pContext,
                                 isP2PProbeReqAllowed
                                );
    sme_ReleaseGlobalLock( &pMac->sme );
  }
  return(status);
}

/* ---------------------------------------------------------------------------
    \fn sme_ReportProbeReq
    \brief  API to enable/disable forwarding of probeReq to apps in p2p.
    \param  hHal - The handle returned by macOpen.
    \param  flag: to set the Probe request forwarding to wpa_supplicant in
                  listen state in p2p
    \return eHalStatus
  ---------------------------------------------------------------------------*/

#ifndef WLAN_FEATURE_CONCURRENT_P2P
eHalStatus sme_ReportProbeReq(tHalHandle hHal, tANI_U8 flag)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    do
    {
        //acquire the lock for the sme object
        status = sme_AcquireGlobalLock(&pMac->sme);
        if(HAL_STATUS_SUCCESS(status))
        {
            /* call set in context */
            pMac->p2pContext.probeReqForwarding = flag;
            //release the lock for the sme object
            sme_ReleaseGlobalLock( &pMac->sme );
        }
    } while(0);

    smsLog(pMac, LOGW, "exiting function %s", __func__);

    return(status);
}

/* ---------------------------------------------------------------------------
    \fn sme_updateP2pIe
    \brief  API to set the P2p Ie in p2p context
    \param  hHal - The handle returned by macOpen.
    \param  p2pIe -  Ptr to p2pIe from HDD.
    \param p2pIeLength: length of p2pIe
    \return eHalStatus
  ---------------------------------------------------------------------------*/

eHalStatus sme_updateP2pIe(tHalHandle hHal, void *p2pIe, tANI_U32 p2pIeLength)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
               TRACE_CODE_SME_RX_HDD_UPDATE_P2P_IE, NO_SESSION, 0));
    //acquire the lock for the sme object
    status = sme_AcquireGlobalLock(&pMac->sme);
    if(HAL_STATUS_SUCCESS(status))
    {
        if(NULL != pMac->p2pContext.probeRspIe){
            vos_mem_free(pMac->p2pContext.probeRspIe);
            pMac->p2pContext.probeRspIeLength = 0;
        }

        pMac->p2pContext.probeRspIe = vos_mem_malloc(p2pIeLength);
        if (NULL == pMac->p2pContext.probeRspIe)
        {
            smsLog(pMac, LOGE, "%s: Unable to allocate P2P IE", __func__);
            pMac->p2pContext.probeRspIeLength = 0;
            status = eHAL_STATUS_FAILURE;
        }
        else
        {
            pMac->p2pContext.probeRspIeLength = p2pIeLength;

            sirDumpBuf( pMac, SIR_LIM_MODULE_ID, LOG2,
                        pMac->p2pContext.probeRspIe,
                        pMac->p2pContext.probeRspIeLength );
            vos_mem_copy((tANI_U8 *)pMac->p2pContext.probeRspIe, p2pIe,
                         p2pIeLength);
        }

        //release the lock for the sme object
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    smsLog(pMac, LOG2, "exiting function %s", __func__);

    return(status);
}
#endif

/* ---------------------------------------------------------------------------
    \fn sme_sendAction
    \brief  API to send action frame from supplicant.
    \param  hHal - The handle returned by macOpen.
    \return eHalStatus
  ---------------------------------------------------------------------------*/

eHalStatus sme_sendAction(tHalHandle hHal, tANI_U8 sessionId,
                          const tANI_U8 *pBuf, tANI_U32 len,
                          tANI_U16 wait, tANI_BOOLEAN noack)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
               TRACE_CODE_SME_RX_HDD_SEND_ACTION, sessionId, 0));
    //acquire the lock for the sme object
    status = sme_AcquireGlobalLock(&pMac->sme);
    if(HAL_STATUS_SUCCESS(status))
    {
        p2pSendAction(hHal, sessionId, pBuf, len, wait, noack);
        //release the lock for the sme object
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    smsLog(pMac, LOGW, "exiting function %s", __func__);

    return(status);
}

eHalStatus sme_CancelRemainOnChannel(tHalHandle hHal, tANI_U8 sessionId )
{
  eHalStatus status = eHAL_STATUS_SUCCESS;
  tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

  MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_CANCEL_REMAIN_ONCHAN, sessionId, 0));
  if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
  {
    status = p2pCancelRemainOnChannel (hHal, sessionId);
    sme_ReleaseGlobalLock( &pMac->sme );
  }
  return(status);
}

//Power Save Related
eHalStatus sme_p2pSetPs(tHalHandle hHal, tP2pPsConfig * data)
{
  eHalStatus status = eHAL_STATUS_SUCCESS;
  tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

  if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
  {
    status = p2pSetPs (hHal, data);
    sme_ReleaseGlobalLock( &pMac->sme );
  }
  return(status);
}


/* ---------------------------------------------------------------------------

  \fn    sme_ConfigureRxpFilter

  \brief
    SME will pass this request to lower mac to set/reset the filter on RXP for
    multicast & broadcast traffic.

  \param

    hHal - The handle returned by macOpen.

    filterMask- Currently the API takes a 1 or 0 (set or reset) as filter.
    Basically to enable/disable the filter (to filter "all" mcbc traffic) based
    on this param. In future we can use this as a mask to set various types of
    filters as suggested below:
    FILTER_ALL_MULTICAST:
    FILTER_ALL_BROADCAST:
    FILTER_ALL_MULTICAST_BROADCAST:


  \return eHalStatus


--------------------------------------------------------------------------- */
eHalStatus sme_ConfigureRxpFilter( tHalHandle hHal,
                            tpSirWlanSetRxpFilters  wlanRxpFilterParam)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    vos_msg_t       vosMessage;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                    TRACE_CODE_SME_RX_HDD_CONFIG_RXPFIL, NO_SESSION, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        /* serialize the req through MC thread */
        vosMessage.bodyptr = wlanRxpFilterParam;
        vosMessage.type         = WDA_CFG_RXP_FILTER_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) )
        {
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return(status);
}

/* ---------------------------------------------------------------------------

  \fn    sme_ConfigureSuspendInd

  \brief
    SME will pass this request to lower mac to Indicate that the wlan needs to
    be suspended

  \param

    hHal - The handle returned by macOpen.

    wlanSuspendParam- Depicts the wlan suspend params

    csrReadyToSuspendCallback - Callback to be called when ready to suspend
                                event is received.
    callbackContext  - Context associated with csrReadyToSuspendCallback.

  \return eHalStatus


--------------------------------------------------------------------------- */
eHalStatus sme_ConfigureSuspendInd( tHalHandle hHal,
                          tpSirWlanSuspendParam  wlanSuspendParam,
                          csrReadyToSuspendCallback callback,
                          void *callbackContext)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    vos_msg_t       vosMessage;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                  TRACE_CODE_SME_RX_HDD_CONFIG_SUSPENDIND, NO_SESSION, 0));

    pMac->readyToSuspendCallback = callback;
    pMac->readyToSuspendContext = callbackContext;

    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        /* serialize the req through MC thread */
        vosMessage.bodyptr = wlanSuspendParam;
        vosMessage.type    = WDA_WLAN_SUSPEND_IND;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) ) {
           pMac->readyToSuspendCallback = NULL;
           pMac->readyToSuspendContext = NULL;
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return(status);
}

/* ---------------------------------------------------------------------------

  \fn    sme_ConfigureResumeReq

  \brief
    SME will pass this request to lower mac to Indicate that the wlan needs to
    be Resumed

  \param

    hHal - The handle returned by macOpen.

    wlanResumeParam- Depicts the wlan resume params


  \return eHalStatus


--------------------------------------------------------------------------- */
eHalStatus sme_ConfigureResumeReq( tHalHandle hHal,
                             tpSirWlanResumeParam  wlanResumeParam)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    vos_msg_t       vosMessage;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_CONFIG_RESUMEREQ, NO_SESSION, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        /* serialize the req through MC thread */
        vosMessage.bodyptr = wlanResumeParam;
        vosMessage.type    = WDA_WLAN_RESUME_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) )
        {
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return(status);
}

#ifdef WLAN_FEATURE_EXTWOW_SUPPORT
/* ---------------------------------------------------------------------------

  \fn    sme_ConfigureExtWoW

  \brief
    SME will pass this request to lower mac to configure Extr WoW

  \param

    hHal - The handle returned by macOpen.

    wlanExtParams- Depicts the wlan Ext params

  \return eHalStatus


--------------------------------------------------------------------------- */
eHalStatus sme_ConfigureExtWoW( tHalHandle hHal,
                          tpSirExtWoWParams  wlanExtParams,
                          csrReadyToExtWoWCallback callback,
                             void *callbackContext)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;
    tpSirExtWoWParams MsgPtr = vos_mem_malloc(sizeof(*MsgPtr));

    if (!MsgPtr)
        return eHAL_STATUS_FAILURE;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                  TRACE_CODE_SME_RX_HDD_CONFIG_EXTWOW, NO_SESSION, 0));

    pMac->readyToExtWoWCallback = callback;
    pMac->readyToExtWoWContext = callbackContext;

    if ( eHAL_STATUS_SUCCESS ==
              ( status = sme_AcquireGlobalLock( &pMac->sme ) ) ) {

        /* serialize the req through MC thread */
        vos_mem_copy(MsgPtr, wlanExtParams, sizeof(*MsgPtr));
        vosMessage.bodyptr = MsgPtr;
        vosMessage.type =  WDA_WLAN_EXT_WOW;
        vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) ) {
            pMac->readyToExtWoWCallback = NULL;
            pMac->readyToExtWoWContext = NULL;
            vos_mem_free(MsgPtr);
            status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    } else {
        pMac->readyToExtWoWCallback = NULL;
        pMac->readyToExtWoWContext = NULL;
        vos_mem_free(MsgPtr);
    }

    return(status);
}

/* ---------------------------------------------------------------------------

  \fn    sme_ConfigureAppType1Params

  \brief
   SME will pass this request to lower mac to configure Indoor WoW parameters.

  \param

    hHal - The handle returned by macOpen.

    wlanAppType1Params- Depicts the wlan App Type 1(Indoor) params

  \return eHalStatus


--------------------------------------------------------------------------- */
eHalStatus sme_ConfigureAppType1Params( tHalHandle hHal,
                          tpSirAppType1Params  wlanAppType1Params)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    vos_msg_t       vosMessage;
    tpSirAppType1Params MsgPtr = vos_mem_malloc(sizeof(*MsgPtr));

    if (!MsgPtr)
        return eHAL_STATUS_FAILURE;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                  TRACE_CODE_SME_RX_HDD_CONFIG_APP_TYPE1, NO_SESSION, 0));

    if ( eHAL_STATUS_SUCCESS ==
              ( status = sme_AcquireGlobalLock( &pMac->sme ) ) ) {

        /* serialize the req through MC thread */
        vos_mem_copy(MsgPtr, wlanAppType1Params, sizeof(*MsgPtr));
        vosMessage.bodyptr = MsgPtr;
        vosMessage.type    =  WDA_WLAN_SET_APP_TYPE1_PARAMS;
        vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) ) {
           vos_mem_free(MsgPtr);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    } else {
        vos_mem_free(MsgPtr);
    }

    return(status);
}

/* ---------------------------------------------------------------------------

  \fn    sme_ConfigureAppType2Params

  \brief
   SME will pass this request to lower mac to configure Indoor WoW parameters.

  \param

    hHal - The handle returned by macOpen.

    wlanAppType2Params- Depicts the wlan App Type 2 (Outdoor) params

  \return eHalStatus


--------------------------------------------------------------------------- */
eHalStatus sme_ConfigureAppType2Params( tHalHandle hHal,
                          tpSirAppType2Params  wlanAppType2Params)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    vos_msg_t       vosMessage;
    tpSirAppType2Params MsgPtr = vos_mem_malloc(sizeof(*MsgPtr));

    if (!MsgPtr)
        return eHAL_STATUS_FAILURE;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                  TRACE_CODE_SME_RX_HDD_CONFIG_APP_TYPE2, NO_SESSION, 0));

    if ( eHAL_STATUS_SUCCESS ==
            ( status = sme_AcquireGlobalLock( &pMac->sme ) ) ) {

        /* serialize the req through MC thread */
        vos_mem_copy(MsgPtr, wlanAppType2Params, sizeof(*MsgPtr));
        vosMessage.bodyptr = MsgPtr;
        vosMessage.type =  WDA_WLAN_SET_APP_TYPE2_PARAMS;
        vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) ) {
           vos_mem_free(MsgPtr);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    } else {
        vos_mem_free(MsgPtr);
    }

    return(status);
}
#endif

/* ---------------------------------------------------------------------------

    \fn sme_GetInfraSessionId

    \brief To get the session ID for infra session, if connected
    This is a synchronous API.

    \param hHal - The handle returned by macOpen.

    \return sessionid, -1 if infra session is not connected

  -------------------------------------------------------------------------------*/
tANI_S8 sme_GetInfraSessionId(tHalHandle hHal)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tANI_S8 sessionid = -1;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {

      sessionid = csrGetInfraSessionId( pMac);

      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (sessionid);
}

/* ---------------------------------------------------------------------------

    \fn sme_GetInfraOperationChannel

    \brief To get the operating channel for infra session, if connected
    This is a synchronous API.

    \param hHal - The handle returned by macOpen.
    \param sessionId - the sessionId returned by sme_OpenSession.

    \return operating channel, 0 if infra session is not connected

  -------------------------------------------------------------------------------*/
tANI_U8 sme_GetInfraOperationChannel( tHalHandle hHal, tANI_U8 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tANI_U8 channel = 0;
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {

      channel = csrGetInfraOperationChannel( pMac, sessionId);

      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (channel);
}

/*
 * This routine will return operating channel on which other BSS is operating to
 * be used for concurrency mode.
 * If other BSS is not up or not connected it will return 0.
 */
tANI_U8 sme_GetConcurrentOperationChannel( tHalHandle hHal )
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tANI_U8 channel = 0;
   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {

      channel = csrGetConcurrentOperationChannel( pMac );
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO_HIGH, "%s: "
           " Other Concurrent Channel = %d", __func__,channel);
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (channel);
}
#ifdef FEATURE_WLAN_MCC_TO_SCC_SWITCH
v_U16_t sme_CheckConcurrentChannelOverlap( tHalHandle hHal, v_U16_t sap_ch,
                                 eCsrPhyMode sapPhyMode, v_U8_t cc_switch_mode)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   v_U16_t channel = 0;

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      channel = csrCheckConcurrentChannelOverlap( pMac, sap_ch, sapPhyMode,
                                                  cc_switch_mode);
      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (channel);
}
#endif

#ifdef FEATURE_WLAN_SCAN_PNO
/******************************************************************************
*
* Name: sme_PreferredNetworkFoundInd
*
* Description:
*    Invoke Preferred Network Found Indication
*
* Parameters:
*    hHal - HAL handle for device
*    pMsg - found network description
*
* Returns: eHalStatus
*
******************************************************************************/
eHalStatus sme_PreferredNetworkFoundInd (tHalHandle hHal, void* pMsg)
{
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
   eHalStatus status = eHAL_STATUS_SUCCESS;
   tSirPrefNetworkFoundInd *pPrefNetworkFoundInd = (tSirPrefNetworkFoundInd *)pMsg;
   v_U8_t dumpSsId[SIR_MAC_MAX_SSID_LENGTH + 1];
   tANI_U8 ssIdLength = 0;

   if (NULL == pMsg)
   {
      smsLog(pMac, LOGE, "in %s msg ptr is NULL", __func__);
      return eHAL_STATUS_FAILURE;
   }

   if (pMac->pnoOffload)
   {
      /* Call Preferred Network Found Indication callback routine. */
      if (pMac->pmc.prefNetwFoundCB != NULL)
      {
         pMac->pmc.prefNetwFoundCB(
             pMac->pmc.preferredNetworkFoundIndCallbackContext,
             pPrefNetworkFoundInd);
      }
      return status;
   }

   if (pPrefNetworkFoundInd->ssId.length > 0)
   {
       ssIdLength = CSR_MIN(SIR_MAC_MAX_SSID_LENGTH,
                          pPrefNetworkFoundInd->ssId.length);
       vos_mem_copy(dumpSsId, pPrefNetworkFoundInd->ssId.ssId, ssIdLength);
       dumpSsId[ssIdLength] = 0;
       smsLog(pMac, LOG2, "%s:SSID=%s frame length %d",
           __func__, dumpSsId, pPrefNetworkFoundInd->frameLength);

         /* Flush scan results, So as to avoid indication/updation of
          * stale entries, which may not have aged out during APPS collapse
          */
         sme_ScanFlushResult(hHal,0);

       //Save the frame to scan result
       if (pPrefNetworkFoundInd->mesgLen > sizeof(tSirPrefNetworkFoundInd))
       {
          //we may have a frame
          status = csrScanSavePreferredNetworkFound(pMac,
                                    pPrefNetworkFoundInd);
          if (!HAL_STATUS_SUCCESS(status))
          {
             smsLog(pMac, LOGE, FL(" fail to save preferred network"));
          }
       }
       else
       {
          smsLog(pMac, LOGE, FL(" not enough data length %d needed %zu"),
             pPrefNetworkFoundInd->mesgLen, sizeof(tSirPrefNetworkFoundInd));
       }

       /* Call Preferred Network Found Indication callback routine. */
       if (HAL_STATUS_SUCCESS(status) && (pMac->pmc.prefNetwFoundCB != NULL))
       {
          pMac->pmc.prefNetwFoundCB(
              pMac->pmc.preferredNetworkFoundIndCallbackContext,
              pPrefNetworkFoundInd);
       }
   }
   else
   {
       smsLog(pMac, LOGE, "%s: callback failed - SSID is NULL", __func__);
       status = eHAL_STATUS_FAILURE;
   }

   return(status);
}

#endif // FEATURE_WLAN_SCAN_PNO


eHalStatus sme_GetCfgValidChannels(tHalHandle hHal, tANI_U8 *aValidChannels, tANI_U32 *len)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrGetCfgValidChannels(pMac, aValidChannels, len);
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return (status);
}


/* ---------------------------------------------------------------------------

    \fn sme_SetTxPerTracking

    \brief Set Tx PER tracking configuration parameters

    \param hHal - The handle returned by macOpen.
    \param pTxPerTrackingConf - Tx PER configuration parameters

    \return eHalStatus

  -------------------------------------------------------------------------------*/
eHalStatus sme_SetTxPerTracking(tHalHandle hHal,
                                void (*pCallbackfn) (void *pCallbackContext),
                                void *pCallbackContext,
                                tpSirTxPerTrackingParam pTxPerTrackingParam)
{
    vos_msg_t msg;
    tpSirTxPerTrackingParam pTxPerTrackingParamReq = NULL;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    if ( eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock( &pMac->sme ) )
    {
        pMac->sme.pTxPerHitCallback = pCallbackfn;
        pMac->sme.pTxPerHitCbContext = pCallbackContext;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    // free this memory in failure case or WDA request callback function
    pTxPerTrackingParamReq = vos_mem_malloc(sizeof(tSirTxPerTrackingParam));
    if (NULL == pTxPerTrackingParamReq)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Not able to allocate memory for tSirTxPerTrackingParam", __func__);
        return eHAL_STATUS_FAILURE;
    }

    vos_mem_copy(pTxPerTrackingParamReq, (void*)pTxPerTrackingParam,
                 sizeof(tSirTxPerTrackingParam));
    msg.type = WDA_SET_TX_PER_TRACKING_REQ;
    msg.reserved = 0;
    msg.bodyptr = pTxPerTrackingParamReq;
    MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                    NO_SESSION, msg.type));
    if(VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Not able to post WDA_SET_TX_PER_TRACKING_REQ message to WDA", __func__);
        vos_mem_free(pTxPerTrackingParamReq);
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------

    \fn sme_HandleChangeCountryCode

    \brief Change Country code, Reg Domain and channel list

    \details Country Code Priority
    0 = 11D > Configured Country > NV
    1 = Configured Country > 11D > NV
    If Supplicant country code is priority than 11d is disabled.
    If 11D is enabled, we update the country code after every scan.
    Hence when Supplicant country code is priority, we don't need 11D info.
    Country code from Supplicant is set as current country code.
    User can send reset command XX (instead of country code) to reset the
    country code to default values which is read from NV.
    In case of reset, 11D is enabled and default NV code is Set as current country code
    If 11D is priority,
    Than Supplicant country code code is set to default code. But 11D code is set as current country code

    \param pMac - The handle returned by macOpen.
    \param pMsgBuf - MSG Buffer

    \return eHalStatus

  -------------------------------------------------------------------------------*/
eHalStatus sme_HandleChangeCountryCode(tpAniSirGlobal pMac,  void *pMsgBuf)
{
   eHalStatus  status = eHAL_STATUS_SUCCESS;
   tAniChangeCountryCodeReq *pMsg;
   v_REGDOMAIN_t domainIdIoctl;
   VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
   static uNvTables nvTables;
   pMsg = (tAniChangeCountryCodeReq *)pMsgBuf;


   /* if the reset Supplicant country code command is triggered, enable 11D, reset the NV country code and return */
   if( VOS_TRUE == vos_mem_compare(pMsg->countryCode, SME_INVALID_COUNTRY_CODE, 2) )
   {
       pMac->roam.configParam.Is11dSupportEnabled = pMac->roam.configParam.Is11dSupportEnabledOriginal;

       vosStatus = vos_nv_readDefaultCountryTable( &nvTables );

       /* read the country code from NV and use it */
       if ( VOS_IS_STATUS_SUCCESS(vosStatus) )
       {
           vos_mem_copy(pMsg->countryCode,
                        nvTables.defaultCountryTable.countryCode,
                        WNI_CFG_COUNTRY_CODE_LEN);
       }
       else
       {
           status = eHAL_STATUS_FAILURE;
           return status;
       }
       /*
        * Update the 11d country to default country from NV bin so that when
        * callback is received for this default country, driver will not
        * disable the 11d taking it as valid country by user.
        */
        smsLog(pMac, LOG1,
         FL
         ("Set default country code (%c%c) from NV as invalid country received"),
         pMsg->countryCode[0],pMsg->countryCode[1]);
        vos_mem_copy(pMac->scan.countryCode11d, pMsg->countryCode,
                      WNI_CFG_COUNTRY_CODE_LEN);

   }
   else
   {
       /* if Supplicant country code has priority, disable 11d */
       if(pMac->roam.configParam.fSupplicantCountryCodeHasPriority &&
         pMsg->countryFromUserSpace)
       {
           pMac->roam.configParam.Is11dSupportEnabled = eANI_BOOLEAN_FALSE;
       }
   }

   /* WEXT set country code means
    * 11D should be supported?
    * 11D Channel should be enforced?
    * 11D Country code should be matched?
    * 11D Reg Domain should be matched?
    * Country string changed */
   if(pMac->roam.configParam.Is11dSupportEnabled &&
      pMac->roam.configParam.fEnforce11dChannels &&
      pMac->roam.configParam.fEnforceCountryCodeMatch &&
      pMac->roam.configParam.fEnforceDefaultDomain &&
      !csrSave11dCountryString(pMac, pMsg->countryCode, eANI_BOOLEAN_TRUE))
   {
      /* All 11D related options are already enabled
       * Country string is not changed
       * Do not need do anything for country code change request */
      return eHAL_STATUS_SUCCESS;
   }

   /* Set Current Country code and Current Regulatory domain */
   status = csrSetCountryCode(pMac, pMsg->countryCode, NULL);
   if(eHAL_STATUS_SUCCESS != status)
   {
       /* Supplicant country code failed. So give 11D priority */
       pMac->roam.configParam.Is11dSupportEnabled = pMac->roam.configParam.Is11dSupportEnabledOriginal;
       smsLog(pMac, LOGE, "Set Country Code Fail %d", status);
       return status;
   }

   /* Overwrite the default country code */
   vos_mem_copy(pMac->scan.countryCodeDefault,
                pMac->scan.countryCodeCurrent,
                WNI_CFG_COUNTRY_CODE_LEN);

   /* Get Domain ID from country code */
   status = csrGetRegulatoryDomainForCountry(pMac,
                   pMac->scan.countryCodeCurrent,
                   (v_REGDOMAIN_t *) &domainIdIoctl,
                   COUNTRY_QUERY);
   if ( status != eHAL_STATUS_SUCCESS )
   {
       smsLog( pMac, LOGE, FL("  fail to get regId %d"), domainIdIoctl );
       return status;
   }
   else if (REGDOMAIN_WORLD == domainIdIoctl)
   {
       /* Supplicant country code is invalid, so we are on world mode now. So
          give 11D chance to update */
       pMac->roam.configParam.Is11dSupportEnabled = pMac->roam.configParam.Is11dSupportEnabledOriginal;
       smsLog(pMac, LOG1, FL("Country Code unrecognized by driver"));
   }


   status = WDA_SetRegDomain(pMac, domainIdIoctl, pMsg->sendRegHint);

   if ( status != eHAL_STATUS_SUCCESS )
   {
       smsLog( pMac, LOGE, FL("  fail to set regId %d"), domainIdIoctl );
       return status;
   }
   else
   {
       //if 11d has priority, clear currentCountryBssid & countryCode11d to get
       //set again if we find AP with 11d info during scan
       if (!pMac->roam.configParam.fSupplicantCountryCodeHasPriority)
       {
           smsLog( pMac, LOGW, FL("Clearing currentCountryBssid, countryCode11d"));
           vos_mem_zero(&pMac->scan.currentCountryBssid, sizeof(tCsrBssid));
           vos_mem_zero( pMac->scan.countryCode11d, sizeof( pMac->scan.countryCode11d ) );
       }
   }

   if( pMsg->changeCCCallback )
   {
      ((tSmeChangeCountryCallback)(pMsg->changeCCCallback))((void *)pMsg->pDevContext);
   }

   return eHAL_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------

    \fn sme_HandleChangeCountryCodeByUser

    \brief Change Country code, Reg Domain and channel list

    If Supplicant country code is priority than 11d is disabled.
    If 11D is enabled, we update the country code after every scan.
    Hence when Supplicant country code is priority, we don't need 11D info.
    Country code from Supplicant is set as current country code.

    \param pMac - The handle returned by macOpen.
    \param pMsg - Carrying new CC & domain set in kernel by user

    \return eHalStatus

  -------------------------------------------------------------------------------*/
eHalStatus sme_HandleChangeCountryCodeByUser(tpAniSirGlobal pMac,
                                             tAniGenericChangeCountryCodeReq *pMsg)
{
    eHalStatus  status = eHAL_STATUS_SUCCESS;
    v_REGDOMAIN_t reg_domain_id;
    v_BOOL_t is11dCountry = VOS_FALSE;

    smsLog(pMac, LOG1, FL(" called"));
    reg_domain_id =  (v_REGDOMAIN_t)pMsg->domain_index;

    if (memcmp(pMsg->countryCode, pMac->scan.countryCode11d,
               VOS_COUNTRY_CODE_LEN) == 0)
    {
        is11dCountry = VOS_TRUE;
    }

    /* Set the country code given by user space when 11dOriginal is FALSE
     * when 11doriginal is True,is11dCountry =0 and
     * fSupplicantCountryCodeHasPriority = 0, then revert the country code,
     * and return failure
     */
    if(pMac->roam.configParam.Is11dSupportEnabledOriginal == true)
    {
        if ((!is11dCountry) && (!pMac->roam.configParam.fSupplicantCountryCodeHasPriority))
        {

            smsLog( pMac, LOGW, FL(" incorrect country being set, nullify this request"));

            /* we have got a request for a country that should not have been added since the
             * STA is associated; nullify this request.
             * If both countryCode11d[0] and countryCode11d[1] are zero, revert it to World
             * domain to avoid from causing cfg80211 call trace.
             */
            if ((pMac->scan.countryCode11d[0] == 0) && (pMac->scan.countryCode11d[1] == 0))
               status = csrGetRegulatoryDomainForCountry(pMac,
                                                  "00",
                                                  (v_REGDOMAIN_t *) &reg_domain_id,
                                                  COUNTRY_IE);
            else
               status = csrGetRegulatoryDomainForCountry(pMac,
                                                  pMac->scan.countryCode11d,
                                                  (v_REGDOMAIN_t *) &reg_domain_id,
                                                  COUNTRY_IE);

            return eHAL_STATUS_FAILURE;
        }
    }
    /* if Supplicant country code has priority, disable 11d */
    if (!is11dCountry && pMac->roam.configParam.fSupplicantCountryCodeHasPriority)
    {
        pMac->roam.configParam.Is11dSupportEnabled = eANI_BOOLEAN_FALSE;
    }

    vos_mem_copy(pMac->scan.countryCodeCurrent, pMsg->countryCode,
                  WNI_CFG_COUNTRY_CODE_LEN);

    status = WDA_SetRegDomain(pMac, reg_domain_id, eSIR_TRUE);

    if (VOS_FALSE == is11dCountry )
    {
        /* Overwrite the default country code */
        vos_mem_copy(pMac->scan.countryCodeDefault,
                      pMac->scan.countryCodeCurrent, WNI_CFG_COUNTRY_CODE_LEN);
        /* set to default domain ID */
        pMac->scan.domainIdDefault = pMac->scan.domainIdCurrent;
    }

    if ( status != eHAL_STATUS_SUCCESS )
    {
        smsLog( pMac, LOGE, FL("  fail to set regId %d"), reg_domain_id );
        return status;
    }
    else
    {
        //if 11d has priority, clear currentCountryBssid & countryCode11d to get
        //set again if we find AP with 11d info during scan
        if((!pMac->roam.configParam.fSupplicantCountryCodeHasPriority) &&
           (VOS_FALSE == is11dCountry ))
        {
            smsLog( pMac, LOGW, FL("Clearing currentCountryBssid, countryCode11d"));
            vos_mem_zero(&pMac->scan.currentCountryBssid, sizeof(tCsrBssid));
            vos_mem_zero( pMac->scan.countryCode11d, sizeof( pMac->scan.countryCode11d ) );
        }
    }

    /* get the channels based on new cc */
    status = csrInitGetChannels(pMac);

    if ( status != eHAL_STATUS_SUCCESS )
    {
        smsLog( pMac, LOGE, FL("  fail to get Channels "));
        return status;
    }

    /* reset info based on new cc, and we are done */
    csrResetCountryInformation(pMac, eANI_BOOLEAN_TRUE, eANI_BOOLEAN_TRUE);
    if (VOS_TRUE == is11dCountry)
    {
        pMac->scan.f11dInfoApplied = eANI_BOOLEAN_TRUE;
        pMac->scan.f11dInfoReset = eANI_BOOLEAN_FALSE;
    }
    /* Country code  Changed, Purge Only scan result
     * which does not have channel number belong to 11d
     * channel list
     */
    csrScanFilterResults(pMac);
    // Do active scans after the country is set by User hints or Country IE
    pMac->scan.curScanType = eSIR_ACTIVE_SCAN;

    sme_DisconnectConnectedSessions(pMac);

    smsLog(pMac, LOG1, FL(" returned"));
    return eHAL_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------

    \fn sme_HandleChangeCountryCodeByCore

    \brief Update Country code in the driver if set by kernel as world

    If 11D is enabled, we update the country code after every scan & notify kernel.
    This is to make sure kernel & driver are in sync in case of CC found in
    driver but not in kernel database

    \param pMac - The handle returned by macOpen.
    \param pMsg - Carrying new CC set in kernel

    \return eHalStatus

  -------------------------------------------------------------------------------*/
eHalStatus sme_HandleChangeCountryCodeByCore(tpAniSirGlobal pMac, tAniGenericChangeCountryCodeReq *pMsg)
{
    eHalStatus  status;

    smsLog(pMac, LOG1, FL(" called"));

    //this is to make sure kernel & driver are in sync in case of CC found in
    //driver but not in kernel database
    if (('0' == pMsg->countryCode[0]) && ('0' == pMsg->countryCode[1]))
    {
        smsLog( pMac, LOGW, FL("Setting countryCode11d & countryCodeCurrent to world CC"));
        vos_mem_copy(pMac->scan.countryCode11d, pMsg->countryCode,
                      WNI_CFG_COUNTRY_CODE_LEN);
        vos_mem_copy(pMac->scan.countryCodeCurrent, pMsg->countryCode,
                      WNI_CFG_COUNTRY_CODE_LEN);
    }

    status = WDA_SetRegDomain(pMac, REGDOMAIN_WORLD, eSIR_TRUE);

    if ( status != eHAL_STATUS_SUCCESS )
    {
        smsLog( pMac, LOGE, FL("  fail to set regId") );
        return status;
    }
    else
    {
        status = csrInitGetChannels(pMac);
        if ( status != eHAL_STATUS_SUCCESS )
        {
            smsLog( pMac, LOGE, FL("  fail to get Channels "));
        }
        else
        {
            csrInitChannelList(pMac);
        }
    }
    /* Country code  Changed, Purge Only scan result
     * which does not have channel number belong to 11d
     * channel list
     */
    csrScanFilterResults(pMac);
    smsLog(pMac, LOG1, FL(" returned"));
    return eHAL_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------

    \fn sme_DisconnectConnectedSessions

    \brief Disconnect STA and P2P client session if channel is not supported

    If new country code does not support the channel on which STA/P2P client
    is connected, it sends the disconnect to the AP/P2P GO

    \param pMac - The handle returned by macOpen

    \return void

  -------------------------------------------------------------------------------*/

void sme_DisconnectConnectedSessions(tpAniSirGlobal pMac)
{
    v_U8_t i,sessionId, isChanFound = false;
    tANI_U8 currChannel;

    for (sessionId=0; sessionId< CSR_ROAM_SESSION_MAX; sessionId++)
    {
        if (csrIsSessionClientAndConnected(pMac, sessionId))
        {
            isChanFound = false;
            //Session is connected.Check the channel
            currChannel = csrGetInfraOperationChannel(pMac, sessionId);
            smsLog(pMac, LOGW, "Current Operating channel : %d, session :%d",
            currChannel, sessionId);
            for (i=0; i < pMac->scan.base20MHzChannels.numChannels; i++)
            {
                if (pMac->scan.base20MHzChannels.channelList[i] == currChannel)
                {
                    isChanFound = true;
                    break;
                }
            }

            if (!isChanFound)
            {
                for (i=0; i < pMac->scan.base40MHzChannels.numChannels; i++)
                {
                    if(pMac->scan.base40MHzChannels.channelList[i] == currChannel)
                    {
                        isChanFound = true;
                        break;
                    }
                }
            }

            if (!isChanFound)
            {
                smsLog(pMac, LOGW, "%s : Disconnect Session :%d", __func__, sessionId);
                csrRoamDisconnect(pMac, sessionId, eCSR_DISCONNECT_REASON_UNSPECIFIED);
            }
        }
    }
}


/* ---------------------------------------------------------------------------

    \fn sme_HandleGenericChangeCountryCode

    \brief Change Country code, Reg Domain and channel list

    If Supplicant country code is priority than 11d is disabled.
    If 11D is enabled, we update the country code after every scan.
    Hence when Supplicant country code is priority, we don't need 11D info.
    Country code from kernel is set as current country code.

    \param pMac - The handle returned by macOpen.
    \param pMsgBuf - message buffer

    \return eHalStatus

  -------------------------------------------------------------------------------*/
eHalStatus sme_HandleGenericChangeCountryCode(tpAniSirGlobal pMac,  void *pMsgBuf)
{
    tAniGenericChangeCountryCodeReq *pMsg;
    v_REGDOMAIN_t reg_domain_id;

    smsLog(pMac, LOG1, FL(" called"));
    pMsg = (tAniGenericChangeCountryCodeReq *)pMsgBuf;
    reg_domain_id =  (v_REGDOMAIN_t)pMsg->domain_index;

    if (REGDOMAIN_COUNT == reg_domain_id)
    {
        sme_HandleChangeCountryCodeByCore(pMac, pMsg);
    }
    else
    {
        sme_HandleChangeCountryCodeByUser(pMac, pMsg);
    }
    smsLog(pMac, LOG1, FL(" returned"));
    return eHAL_STATUS_SUCCESS;
}

#ifdef WLAN_FEATURE_PACKET_FILTERING
eHalStatus sme_8023MulticastList (tHalHandle hHal, tANI_U8 sessionId, tpSirRcvFltMcAddrList pMulticastAddrs)
{
    tpSirRcvFltMcAddrList   pRequestBuf;
    vos_msg_t               msg;
    tpAniSirGlobal          pMac = PMAC_STRUCT(hHal);
    tCsrRoamSession         *pSession = NULL;

    VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO, "%s: "
               "ulMulticastAddrCnt=%d, multicastAddr[0]=%p", __func__,
               pMulticastAddrs->ulMulticastAddrCnt,
               pMulticastAddrs->multicastAddr[0]);

    /*
     *Find the connected Infra / P2P_client connected session
    */
    if (CSR_IS_SESSION_VALID(pMac, sessionId) &&
        csrIsConnStateInfra(pMac, sessionId))
    {
        pSession = CSR_GET_SESSION( pMac, sessionId );
    }

    if (pSession == NULL) {
        smsLog(pMac, LOGW, FL("Unable to find the session Id: %d"), sessionId);
        return eHAL_STATUS_FAILURE;
    }

    pRequestBuf = vos_mem_malloc(sizeof(tSirRcvFltMcAddrList));
    if (NULL == pRequestBuf)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Not able to "
            "allocate memory for 8023 Multicast List request", __func__);
        return eHAL_STATUS_FAILED_ALLOC;
    }

    if( !csrIsConnStateConnectedInfra (pMac, sessionId ))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Ignoring the "
                       "indication as we are not connected", __func__);
        vos_mem_free(pRequestBuf);
        return eHAL_STATUS_FAILURE;
    }

    vos_mem_copy(pRequestBuf, pMulticastAddrs, sizeof(tSirRcvFltMcAddrList));

    vos_mem_copy(pRequestBuf->selfMacAddr, pSession->selfMacAddr,
                 sizeof(tSirMacAddr));
    vos_mem_copy(pRequestBuf->bssId, pSession->connectedProfile.bssid,
                 sizeof(tSirMacAddr));

    msg.type = WDA_8023_MULTICAST_LIST_REQ;
    msg.reserved = 0;
    msg.bodyptr = pRequestBuf;
    MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG, sessionId,
                                                          msg.type));
    if(VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Not able to "
            "post WDA_8023_MULTICAST_LIST message to WDA", __func__);
        vos_mem_free(pRequestBuf);
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}

eHalStatus sme_ReceiveFilterSetFilter(tHalHandle hHal, tpSirRcvPktFilterCfgType pRcvPktFilterCfg,
                                           tANI_U8 sessionId)
{
    tpSirRcvPktFilterCfgType    pRequestBuf;
    v_SINT_t                allocSize;
    vos_msg_t               msg;
    tpAniSirGlobal          pMac = PMAC_STRUCT(hHal);
    tCsrRoamSession         *pSession = CSR_GET_SESSION( pMac, sessionId );
    v_U8_t   idx=0;

    VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO, "%s: filterType=%d, "
               "filterId = %d", __func__,
               pRcvPktFilterCfg->filterType, pRcvPktFilterCfg->filterId);

    allocSize = sizeof(tSirRcvPktFilterCfgType);

    pRequestBuf = vos_mem_malloc(allocSize);

    if (NULL == pRequestBuf)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Not able to "
            "allocate memory for Receive Filter Set Filter request", __func__);
        return eHAL_STATUS_FAILED_ALLOC;
    }

    if( NULL == pSession )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Session Not found ", __func__);
        vos_mem_free(pRequestBuf);
        return eHAL_STATUS_FAILURE;
    }

    vos_mem_copy(pRcvPktFilterCfg->selfMacAddr, pSession->selfMacAddr,
                 sizeof(tSirMacAddr));
    vos_mem_copy(pRcvPktFilterCfg->bssId, pSession->connectedProfile.bssid,
                 sizeof(tSirMacAddr));
    vos_mem_copy(pRequestBuf, pRcvPktFilterCfg, allocSize);

    msg.type = WDA_RECEIVE_FILTER_SET_FILTER_REQ;
    msg.reserved = 0;
    msg.bodyptr = pRequestBuf;
    MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG, sessionId,
                                                           msg.type));
    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO, "Pkt Flt Req : "
           "FT %d FID %d ",
           pRequestBuf->filterType, pRequestBuf->filterId);

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO, "Pkt Flt Req : "
           "params %d CT %d",
           pRequestBuf->numFieldParams, pRequestBuf->coalesceTime);

    for (idx=0; idx<pRequestBuf->numFieldParams; idx++)
    {

      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
           "Proto %d Comp Flag %d ",
           pRequestBuf->paramsData[idx].protocolLayer,
           pRequestBuf->paramsData[idx].cmpFlag);

      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
           "Data Offset %d Data Len %d",
           pRequestBuf->paramsData[idx].dataOffset,
           pRequestBuf->paramsData[idx].dataLength);

      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
          "CData: %d:%d:%d:%d:%d:%d",
           pRequestBuf->paramsData[idx].compareData[0],
           pRequestBuf->paramsData[idx].compareData[1],
           pRequestBuf->paramsData[idx].compareData[2],
           pRequestBuf->paramsData[idx].compareData[3],
           pRequestBuf->paramsData[idx].compareData[4],
           pRequestBuf->paramsData[idx].compareData[5]);

      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
           "MData: %d:%d:%d:%d:%d:%d",
           pRequestBuf->paramsData[idx].dataMask[0],
           pRequestBuf->paramsData[idx].dataMask[1],
           pRequestBuf->paramsData[idx].dataMask[2],
           pRequestBuf->paramsData[idx].dataMask[3],
           pRequestBuf->paramsData[idx].dataMask[4],
           pRequestBuf->paramsData[idx].dataMask[5]);

    }

    if(VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Not able to post "
            "WDA_RECEIVE_FILTER_SET_FILTER message to WDA", __func__);
        vos_mem_free(pRequestBuf);
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}

eHalStatus sme_GetFilterMatchCount(tHalHandle hHal,
                                   FilterMatchCountCallback callbackRoutine,
                                   void *callbackContext,
                                   tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status;

    VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO, "+%s", __func__);

    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme)))
    {
        pmcGetFilterMatchCount(hHal, callbackRoutine, callbackContext, sessionId);
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO, "-%s", __func__);

    return (status);
}

eHalStatus sme_ReceiveFilterClearFilter(tHalHandle hHal, tpSirRcvFltPktClearParam pRcvFltPktClearParam,
                                             tANI_U8 sessionId)
{
    tpSirRcvFltPktClearParam pRequestBuf;
    vos_msg_t               msg;
    tpAniSirGlobal          pMac = PMAC_STRUCT(hHal);
    tCsrRoamSession         *pSession = CSR_GET_SESSION( pMac, sessionId );

    VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO, "%s: filterId = %d", __func__,
               pRcvFltPktClearParam->filterId);

    pRequestBuf = vos_mem_malloc(sizeof(tSirRcvFltPktClearParam));
    if (NULL == pRequestBuf)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
            "%s: Not able to allocate memory for Receive Filter "
            "Clear Filter request", __func__);
        return eHAL_STATUS_FAILED_ALLOC;
    }
    if( NULL == pSession )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
            "%s: Session Not find ", __func__);
        vos_mem_free(pRequestBuf);
        return eHAL_STATUS_FAILURE;
    }

    vos_mem_copy(pRcvFltPktClearParam->selfMacAddr, pSession->selfMacAddr,
                 sizeof(tSirMacAddr));
    vos_mem_copy(pRcvFltPktClearParam->bssId, pSession->connectedProfile.bssid,
                 sizeof(tSirMacAddr));

    vos_mem_copy(pRequestBuf, pRcvFltPktClearParam, sizeof(tSirRcvFltPktClearParam));

    msg.type = WDA_RECEIVE_FILTER_CLEAR_FILTER_REQ;
    msg.reserved = 0;
    msg.bodyptr = pRequestBuf;
    MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG, sessionId,
                                                          msg.type));
    if(VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: Not able to post "
            "WDA_RECEIVE_FILTER_CLEAR_FILTER message to WDA", __func__);
        vos_mem_free(pRequestBuf);
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}
#endif // WLAN_FEATURE_PACKET_FILTERING

/* ---------------------------------------------------------------------------
    \fn sme_PreChannelSwitchIndFullPowerCB
    \brief  call back function for the PMC full power request because of pre
             channel switch.
    \param callbackContext
    \param status
  ---------------------------------------------------------------------------*/
void sme_PreChannelSwitchIndFullPowerCB(void *callbackContext,
                eHalStatus status)
{
    tpAniSirGlobal pMac = (tpAniSirGlobal)callbackContext;
    tSirMbMsg *pMsg;
    tANI_U16 msgLen;

    msgLen = (tANI_U16)(sizeof( tSirMbMsg ));
    pMsg = vos_mem_malloc(msgLen);
    if ( NULL != pMsg )
    {
        vos_mem_set(pMsg, msgLen, 0);
        pMsg->type = pal_cpu_to_be16((tANI_U16)eWNI_SME_PRE_CHANNEL_SWITCH_FULL_POWER);
        pMsg->msgLen = pal_cpu_to_be16(msgLen);
        status = palSendMBMessage(pMac->hHdd, pMsg);
    }

    return;
}

/* ---------------------------------------------------------------------------
    \fn sme_PreChannelSwitchIndOffloadFullPowerCB
    \brief  call back function for the PMC full power request because of pre
             channel switch for offload case.
    \param callbackContext
    \param sessionId
    \param status
  ---------------------------------------------------------------------------*/
void sme_PreChannelSwitchIndOffloadFullPowerCB(void *callbackContext,tANI_U32 sessionId,
                eHalStatus status)
{
    tpAniSirGlobal pMac = (tpAniSirGlobal)callbackContext;
    tSirMbMsg *pMsg;
    tANI_U16 msgLen;

    msgLen = (tANI_U16)(sizeof( tSirMbMsg ));
    pMsg = vos_mem_malloc(msgLen);
    if ( NULL != pMsg )
    {
        vos_mem_set(pMsg, msgLen, 0);
        pMsg->type = pal_cpu_to_be16((tANI_U16)eWNI_SME_PRE_CHANNEL_SWITCH_FULL_POWER);
        pMsg->msgLen = pal_cpu_to_be16(msgLen);
        status = palSendMBMessage(pMac->hHdd, pMsg);
    }

    return;
}

/* ---------------------------------------------------------------------------
    \fn sme_HandlePreChannelSwitchInd
    \brief  Processes the indication from PE for pre-channel switch.
    \param hHal
    \param void *pMsgBuf to carry session id
    \- The handle returned by macOpen. return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_HandlePreChannelSwitchInd(tHalHandle hHal, void *pMsgBuf)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   tpSirSmePreSwitchChannelInd pPreSwitchChInd = (tpSirSmePreSwitchChannelInd)pMsgBuf;

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {

        if(!pMac->psOffloadEnabled)
        {
            status = pmcRequestFullPower(hHal, sme_PreChannelSwitchIndFullPowerCB,
                            pMac, eSME_FULL_PWR_NEEDED_BY_CHANNEL_SWITCH);
        }
        else
        {
            if (NULL != pPreSwitchChInd)
            {
                status = pmcOffloadRequestFullPower(hHal, pPreSwitchChInd->sessionId,
                                                    sme_PreChannelSwitchIndOffloadFullPowerCB,
                                                    pMac, eSME_FULL_PWR_NEEDED_BY_CHANNEL_SWITCH);
            }
            else
	    {
                   smsLog(pMac, LOGE, "Empty pMsgBuf  message for channel switch "
                          "(eWNI_SME_PRE_SWITCH_CHL_IND), nothing to process");
            }
        }

       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_HandlePostChannelSwitchInd
    \brief  Processes the indication from PE for post-channel switch.
    \param hHal
    \- The handle returned by macOpen. return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_HandlePostChannelSwitchInd(tHalHandle hHal)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status = pmcRequestBmps(hHal, NULL, NULL);
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (status);
}

/* ---------------------------------------------------------------------------

    \fn sme_IsChannelValid

    \brief To check if the channel is valid for currently established domain
    This is a synchronous API.

    \param hHal - The handle returned by macOpen.
    \param channel - channel to verify

    \return TRUE/FALSE, TRUE if channel is valid

  -------------------------------------------------------------------------------*/
tANI_BOOLEAN sme_IsChannelValid(tHalHandle hHal, tANI_U8 channel)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tANI_BOOLEAN valid = FALSE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {

      valid = csrRoamIsChannelValid( pMac, channel);

      sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (valid);
}

/* ---------------------------------------------------------------------------
    \fn sme_SetFreqBand
    \brief  Used to set frequency band.
    \param  hHal
    \param  sessionId - Session Identifier
    \eBand  band value to be configured
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_SetFreqBand(tHalHandle hHal, tANI_U8 sessionId, eCsrBand eBand)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      status = csrSetBand(hHal, sessionId, eBand);
      sme_ReleaseGlobalLock( &pMac->sme );
   }
   return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_GetFreqBand
    \brief  Used to get the current band settings.
    \param  hHal
    \pBand  pointer to hold band value
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_GetFreqBand(tHalHandle hHal, eCsrBand *pBand)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
      *pBand = csrGetCurrentBand( hHal );
      sme_ReleaseGlobalLock( &pMac->sme );
   }
   return status;
}

#ifdef WLAN_WAKEUP_EVENTS
/******************************************************************************
  \fn sme_WakeReasonIndCallback

  \brief
  a callback function called when SME received eWNI_SME_WAKE_REASON_IND event from WDA

  \param hHal - HAL handle for device
  \param pMsg - Message body passed from WDA; includes Wake Reason Indication parameter

  \return eHalStatus
******************************************************************************/
eHalStatus sme_WakeReasonIndCallback (tHalHandle hHal, void* pMsg)
{
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
   eHalStatus status = eHAL_STATUS_SUCCESS;
   tSirWakeReasonInd *pWakeReasonInd = (tSirWakeReasonInd *)pMsg;

   if (NULL == pMsg)
   {
      smsLog(pMac, LOGE, "in %s msg ptr is NULL", __func__);
      status = eHAL_STATUS_FAILURE;
   }
   else
   {
      smsLog(pMac, LOG2, "SME: entering sme_WakeReasonIndCallback");

      /* Call Wake Reason Indication callback routine. */
      if (pMac->pmc.wakeReasonIndCB != NULL)
          pMac->pmc.wakeReasonIndCB(pMac->pmc.wakeReasonIndCBContext, pWakeReasonInd);

      pMac->pmc.wakeReasonIndCB = NULL;
      pMac->pmc.wakeReasonIndCBContext = NULL;

      smsLog(pMac, LOG1, "Wake Reason Indication in %s(), reason=%d", __func__, pWakeReasonInd->ulReason);
   }

   return(status);
}
#endif // WLAN_WAKEUP_EVENTS


/* ---------------------------------------------------------------------------
    \fn sme_SetMaxTxPowerPerBand

    \brief Set the Maximum Transmit Power specific to band dynamically.
    Note: this setting will not persist over reboots.

    \param band
    \param power to set in dB
    \- return eHalStatus

  ----------------------------------------------------------------------------*/
eHalStatus sme_SetMaxTxPowerPerBand(eCsrBand band, v_S7_t dB)
{
    vos_msg_t msg;
    tpMaxTxPowerPerBandParams pMaxTxPowerPerBandParams = NULL;

    pMaxTxPowerPerBandParams = vos_mem_malloc(sizeof(tMaxTxPowerPerBandParams));
    if (NULL == pMaxTxPowerPerBandParams)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s:Not able to allocate memory for pMaxTxPowerPerBandParams",
                  __func__);
        return eHAL_STATUS_FAILURE;
    }

    pMaxTxPowerPerBandParams->power = dB;
    pMaxTxPowerPerBandParams->bandInfo = band;

    msg.type = WDA_SET_MAX_TX_POWER_PER_BAND_REQ;
    msg.reserved = 0;
    msg.bodyptr = pMaxTxPowerPerBandParams;
    MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG, NO_SESSION,
                                                          msg.type));
    if (VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s:Not able to post WDA_SET_MAX_TX_POWER_PER_BAND_REQ",
                  __func__);
        vos_mem_free(pMaxTxPowerPerBandParams);
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}

/**
 * sme_SetMaxTxPower() - Set the Maximum Transmit Power
 *
 * @hHal: hal pointer.
 * @bssid: bssid to set the power cap for
 * @self_mac_addr:self mac address
 * @db: power to set in dB
 *
 * Set the maximum transmit power dynamically.
 *
 * Return: eHalStatus
 *
 */
eHalStatus sme_SetMaxTxPower(tHalHandle hHal, tSirMacAddr bssid,
				tSirMacAddr self_mac_addr, v_S7_t db)
{
	tpAniSirGlobal mac_ptr = PMAC_STRUCT(hHal);
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tSmeCmd *set_max_tx_pwr;

	MTRACE(vos_trace(VOS_MODULE_ID_SME,
		TRACE_CODE_SME_RX_HDD_SET_MAXTXPOW, NO_SESSION, 0));
	smsLog(mac_ptr, LOG1,
	  FL("bssid :" MAC_ADDRESS_STR " self addr: "MAC_ADDRESS_STR" power %d Db"),
	  MAC_ADDR_ARRAY(bssid), MAC_ADDR_ARRAY(self_mac_addr), db);

	status = sme_AcquireGlobalLock(&mac_ptr->sme);
	if (HAL_STATUS_SUCCESS(status)) {
		set_max_tx_pwr = csrGetCommandBuffer(mac_ptr);
		if (set_max_tx_pwr) {
			set_max_tx_pwr->command = eSmeCommandSetMaxTxPower;
			vos_mem_copy(set_max_tx_pwr->u.set_tx_max_pwr.bssid,
				bssid, SIR_MAC_ADDR_LENGTH);
			vos_mem_copy(set_max_tx_pwr->u.set_tx_max_pwr.self_sta_mac_addr,
				self_mac_addr, SIR_MAC_ADDR_LENGTH);
			set_max_tx_pwr->u.set_tx_max_pwr.power = db;
			status = csrQueueSmeCommand(mac_ptr, set_max_tx_pwr,
							eANI_BOOLEAN_TRUE);
			if (!HAL_STATUS_SUCCESS(status)) {
				smsLog(mac_ptr, LOGE,
					FL("fail to send msg status = %d"),
									status);
				csrReleaseCommandScan(mac_ptr, set_max_tx_pwr);
			}
		}
		else
		{
			smsLog(mac_ptr, LOGE,
				FL("can not obtain a common buffer"));
			status = eHAL_STATUS_RESOURCES;
		}
		sme_ReleaseGlobalLock(&mac_ptr->sme);
	}
	return status;
}

/* ---------------------------------------------------------------------------

    \fn sme_SetCustomMacAddr

    \brief Set the customer Mac Address.

    \param customMacAddr  customer MAC Address
    \- return eHalStatus

  ---------------------------------------------------------------------------*/
eHalStatus sme_SetCustomMacAddr(tSirMacAddr customMacAddr)
{
    vos_msg_t msg;
    tSirMacAddr *pBaseMacAddr;

    pBaseMacAddr = vos_mem_malloc(sizeof(tSirMacAddr));
    if (NULL == pBaseMacAddr)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
            FL("Not able to allocate memory for pBaseMacAddr"));
        return eHAL_STATUS_FAILURE;
    }

    vos_mem_copy(*pBaseMacAddr, customMacAddr, sizeof(tSirMacAddr));

    msg.type = SIR_HAL_SET_BASE_MACADDR_IND;
    msg.reserved = 0;
    msg.bodyptr = pBaseMacAddr;

    if (VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
            FL("Not able to post SIR_HAL_SET_BASE_MACADDR_IND message to WDA"));
        vos_mem_free(pBaseMacAddr);
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}

/* ----------------------------------------------------------------------------
   \fn sme_SetTxPower
   \brief Set Transmit Power dynamically.
   \param  hHal
   \param sessionId  Target Session ID
   \pBSSId BSSID
   \dev_mode dev_mode such as station, P2PGO, SAP
   \param dBm  power to set
   \- return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_SetTxPower(tHalHandle hHal, v_U8_t sessionId,
                          tSirMacAddr pBSSId,
                          tVOS_CON_MODE dev_mode, int dBm)
{
   vos_msg_t msg;
   tpMaxTxPowerParams pTxParams = NULL;
   v_S7_t power = (v_S7_t)dBm;

  MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_SET_TXPOW, sessionId, 0));

   /* make sure there is no overflow */
   if ((int)power != dBm) {
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s: error, invalid power = %d", __func__, dBm);
      return eHAL_STATUS_FAILURE;
   }

   pTxParams = vos_mem_malloc(sizeof(tMaxTxPowerParams));
   if (NULL == pTxParams)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
              "%s: Not able to allocate memory for pTxParams", __func__);
       return eHAL_STATUS_FAILURE;
   }

   vos_mem_copy(pTxParams->bssId, pBSSId, SIR_MAC_ADDR_LENGTH);
   pTxParams->power = power; /* unit is dBm */
   pTxParams->dev_mode = dev_mode;
   msg.type = WDA_SET_TX_POWER_REQ;
   msg.reserved = 0;
   msg.bodyptr = pTxParams;

   if (VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))
   {
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s: failed to post WDA_SET_TX_POWER_REQ to WDA",
                __func__);
      vos_mem_free(pTxParams);
      return eHAL_STATUS_FAILURE;
   }

   return eHAL_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------

    \fn sme_HideSSID

    \brief hide/show SSID dynamically. Note: this setting will
    not persist over reboots.

    \param hHal
    \param sessionId
    \param ssidHidden 0 - Broadcast SSID, 1 - Disable broadcast SSID
    \- return eHalStatus

  -------------------------------------------------------------------------------*/
eHalStatus sme_HideSSID(tHalHandle hHal, v_U8_t sessionId, v_U8_t ssidHidden)
{
    eHalStatus status   = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tANI_U16 len;

    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        tpSirUpdateParams pMsg;
        tCsrRoamSession *pSession = CSR_GET_SESSION( pMac, sessionId );

        if(!pSession)
        {
            smsLog(pMac, LOGE, FL("  session %d not found "), sessionId);
            sme_ReleaseGlobalLock( &pMac->sme );
            return eHAL_STATUS_FAILURE;
        }

        if( !pSession->sessionActive )
            VOS_ASSERT(0);

        /* Create the message and send to lim */
        len = sizeof(tSirUpdateParams);
        pMsg = vos_mem_malloc(len);
        if ( NULL == pMsg )
           status = eHAL_STATUS_FAILURE;
        else
        {
            vos_mem_set(pMsg, sizeof(tSirUpdateParams), 0);
            pMsg->messageType     = eWNI_SME_HIDE_SSID_REQ;
            pMsg->length          = len;
            /* Data starts from here */
            pMsg->sessionId       = sessionId;
            pMsg->ssidHidden      = ssidHidden;
            status = palSendMBMessage(pMac->hHdd, pMsg);
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
   return status;
}

/* ---------------------------------------------------------------------------

    \fn sme_SetTmLevel
    \brief  Set Thermal Mitigation Level to RIVA
    \param  hHal - The handle returned by macOpen.
    \param  newTMLevel - new Thermal Mitigation Level
    \param  tmMode - Thermal Mitigation handle mode, default 0
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_SetTmLevel(tHalHandle hHal, v_U16_t newTMLevel, v_U16_t tmMode)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS          vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);
    vos_msg_t           vosMessage;
    tAniSetTmLevelReq  *setTmLevelReq = NULL;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                         TRACE_CODE_SME_RX_HDD_SET_TMLEVEL, NO_SESSION, 0));
    if ( eHAL_STATUS_SUCCESS == ( status = sme_AcquireGlobalLock( &pMac->sme ) ) )
    {
        setTmLevelReq = (tAniSetTmLevelReq *)vos_mem_malloc(sizeof(tAniSetTmLevelReq));
        if (NULL == setTmLevelReq)
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Not able to allocate memory for sme_SetTmLevel", __func__);
           sme_ReleaseGlobalLock( &pMac->sme );
           return eHAL_STATUS_FAILURE;
        }

        setTmLevelReq->tmMode     = tmMode;
        setTmLevelReq->newTmLevel = newTMLevel;

        /* serialize the req through MC thread */
        vosMessage.bodyptr = setTmLevelReq;
        vosMessage.type    = WDA_SET_TM_LEVEL_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
        if ( !VOS_IS_STATUS_SUCCESS(vosStatus) )
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Post Set TM Level MSG fail", __func__);
           vos_mem_free(setTmLevelReq);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return(status);
}

/*---------------------------------------------------------------------------

  \brief sme_featureCapsExchange() - SME interface to exchange capabilities between
  Host and FW.

  \param  hHal - HAL handle for device

  \return NONE

---------------------------------------------------------------------------*/
void sme_featureCapsExchange( tHalHandle hHal)
{
    v_CONTEXT_t vosContext = vos_get_global_context(VOS_MODULE_ID_SME, NULL);
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_CAPS_EXCH, NO_SESSION, 0));
    WDA_featureCapsExchange(vosContext);
}

/*---------------------------------------------------------------------------

  \brief sme_disableFeatureCapablity() - SME interface to disable Active mode
                                       offload capability in Host.

  \param  hHal - HAL handle for device

  \return NONE

---------------------------------------------------------------------------*/
void sme_disableFeatureCapablity(tANI_U8 feature_index)
{
    WDA_disableCapablityFeature(feature_index);
}



/* ---------------------------------------------------------------------------

    \fn sme_GetDefaultCountryCode

    \brief Get the default country code from NV

    \param  hHal
    \param  pCountry
    \- return eHalStatus

  -------------------------------------------------------------------------------*/
eHalStatus sme_GetDefaultCountryCodeFrmNv(tHalHandle hHal, tANI_U8 *pCountry)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_GET_DEFCCNV, NO_SESSION, 0));
    return csrGetDefaultCountryCodeFrmNv(pMac, pCountry);
}

/* ---------------------------------------------------------------------------

    \fn sme_GetCurrentCountryCode

    \brief Get the current country code

    \param  hHal
    \param  pCountry
    \- return eHalStatus

  -------------------------------------------------------------------------------*/
eHalStatus sme_GetCurrentCountryCode(tHalHandle hHal, tANI_U8 *pCountry)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_GET_CURCC, NO_SESSION, 0));
    return csrGetCurrentCountryCode(pMac, pCountry);
}

/* ---------------------------------------------------------------------------
    \fn sme_transportDebug
    \brief  Dynamically monitoring Transport channels
            Private IOCTL will query transport channel status if driver loaded
    \param  hHal Upper MAC context
    \param  displaySnapshot Display transport channel snapshot option
    \param  toggleStallDetect Enable stall detect feature
                              This feature will take effect to data performance
                              Not integrate till fully verification
    \- return NONE
    -------------------------------------------------------------------------*/
void sme_transportDebug(tHalHandle hHal, v_BOOL_t displaySnapshot, v_BOOL_t toggleStallDetect)
{
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

   if (NULL == pMac)
   {
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s: invalid context", __func__);
      return;
   }
   WDA_TransportChannelDebug(pMac, displaySnapshot, toggleStallDetect);
}

/* ---------------------------------------------------------------------------
    \fn     sme_ResetPowerValuesFor5G
    \brief  Reset the power values for 5G band with NV power values.
    \param  hHal - HAL handle for device
    \- return NONE
    -------------------------------------------------------------------------*/
void sme_ResetPowerValuesFor5G (tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT (hHal);
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_RESET_PW5G, NO_SESSION, 0));
    csrSaveChannelPowerForBand(pMac, eANI_BOOLEAN_TRUE);
    csrApplyPower2Current(pMac);     // Store the channel+power info in the global place: Cfg
}

#if  defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)
/* ---------------------------------------------------------------------------
    \fn sme_UpdateRoamPrefer5GHz
    \brief  enable/disable Roam prefer 5G runtime option
            This function is called through dynamic setConfig callback function
            to configure the Roam prefer 5G runtime option
    \param  hHal - HAL handle for device
    \param  nRoamPrefer5GHz Enable/Disable Roam prefer 5G runtime option
    \- return Success or failure
    -------------------------------------------------------------------------*/

eHalStatus sme_UpdateRoamPrefer5GHz(tHalHandle hHal, v_BOOL_t nRoamPrefer5GHz)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                       TRACE_CODE_SME_RX_HDD_UPDATE_RP5G, NO_SESSION, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: gRoamPrefer5GHz is changed from %d to %d", __func__,
                      pMac->roam.configParam.nRoamPrefer5GHz,
                      nRoamPrefer5GHz);
        pMac->roam.configParam.nRoamPrefer5GHz = nRoamPrefer5GHz;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_setRoamIntraBand
    \brief  enable/disable Intra band roaming
            This function is called through dynamic setConfig callback function
            to configure the intra band roaming
    \param  hHal - HAL handle for device
    \param  nRoamIntraBand Enable/Disable Intra band roaming
    \- return Success or failure
    -------------------------------------------------------------------------*/
eHalStatus sme_setRoamIntraBand(tHalHandle hHal, const v_BOOL_t nRoamIntraBand)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_SET_ROAMIBAND, NO_SESSION, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: gRoamIntraBand is changed from %d to %d", __func__,
                      pMac->roam.configParam.nRoamIntraBand,
                      nRoamIntraBand);
        pMac->roam.configParam.nRoamIntraBand = nRoamIntraBand;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_UpdateRoamScanNProbes
    \brief  function to update roam scan N probes
            This function is called through dynamic setConfig callback function
            to update roam scan N probes
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nProbes number of probe requests to be sent out
    \- return Success or failure
    -------------------------------------------------------------------------*/
eHalStatus sme_UpdateRoamScanNProbes(tHalHandle hHal, tANI_U8 sessionId,
                                     const v_U8_t nProbes)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_UPDATE_ROAM_SCAN_N_PROBES, NO_SESSION, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: gRoamScanNProbes is changed from %d to %d", __func__,
                      pMac->roam.configParam.nProbes,
                      nProbes);
        pMac->roam.configParam.nProbes = nProbes;
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
        csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                           REASON_NPROBES_CHANGED);
    }
#endif
    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_UpdateRoamScanHomeAwayTime
    \brief  function to update roam scan Home away time
            This function is called through dynamic setConfig callback function
            to update roam scan home away time
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nRoamScanAwayTime Scan home away time
    \param  bSendOffloadCmd If TRUE then send offload command to firmware
                            If FALSE then command is not sent to firmware
    \- return Success or failure
    -------------------------------------------------------------------------*/
eHalStatus sme_UpdateRoamScanHomeAwayTime(tHalHandle hHal,
                                          tANI_U8 sessionId,
                                          const v_U16_t nRoamScanHomeAwayTime,
                                          const eAniBoolean bSendOffloadCmd)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_UPDATE_ROAM_SCAN_HOME_AWAY_TIME, NO_SESSION, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: gRoamScanHomeAwayTime is changed from %d to %d", __func__,
                      pMac->roam.configParam.nRoamScanHomeAwayTime,
                      nRoamScanHomeAwayTime);
        pMac->roam.configParam.nRoamScanHomeAwayTime = nRoamScanHomeAwayTime;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled && bSendOffloadCmd)
    {
        csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                           REASON_HOME_AWAY_TIME_CHANGED);
    }
#endif
    return status;
}

/**
 * sme_ext_change_channel()- function to post send ECSA
 * action frame to csr.
 * @hHal: Hal context
 * @channel: new channel to switch
 * @session_id: senssion it should be sent on.
 *
 * This function is called to post ECSA frame to csr.
 *
 * Return: success if msg is sent else return failure
 */
eHalStatus sme_ext_change_channel(tHalHandle hHal, uint32_t channel,
						uint8_t session_id)
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal mac_ctx  = PMAC_STRUCT(hHal);
	uint8_t channel_state;

	smsLog(mac_ctx, LOGE, FL(" Set Channel %d "), channel);
	channel_state =
		vos_nv_getChannelEnabledState(channel);

	if ((NV_CHANNEL_DISABLE == channel_state)) {
		smsLog(mac_ctx, LOGE, FL(" Invalid channel %d "), channel);
		return eHAL_STATUS_FAILURE;
	}

	status = sme_AcquireGlobalLock(&mac_ctx->sme);

	if (eHAL_STATUS_SUCCESS == status) {
		/* update the channel list to the firmware */
		status = csr_send_ext_change_channel(mac_ctx,
						channel, session_id);
		sme_ReleaseGlobalLock(&mac_ctx->sme);
	}

	return status;
}
/* ---------------------------------------------------------------------------
    \fn sme_getRoamIntraBand
    \brief  get Intra band roaming
    \param  hHal - HAL handle for device
    \- return Success or failure
    -------------------------------------------------------------------------*/
v_BOOL_t sme_getRoamIntraBand(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
              TRACE_CODE_SME_RX_HDD_GET_ROAMIBAND, NO_SESSION, 0));
    return pMac->roam.configParam.nRoamIntraBand;
}

/* ---------------------------------------------------------------------------
    \fn sme_getRoamScanNProbes
    \brief  get N Probes
    \param  hHal - HAL handle for device
    \- return Success or failure
    -------------------------------------------------------------------------*/
v_U8_t sme_getRoamScanNProbes(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.nProbes;
}

/* ---------------------------------------------------------------------------
    \fn sme_getRoamScanHomeAwayTime
    \brief  get Roam scan home away time
    \param  hHal - HAL handle for device
    \- return Success or failure
    -------------------------------------------------------------------------*/
v_U16_t sme_getRoamScanHomeAwayTime(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.nRoamScanHomeAwayTime;
}


/* ---------------------------------------------------------------------------
    \fn sme_UpdateImmediateRoamRssiDiff
    \brief  Update nImmediateRoamRssiDiff
            This function is called through dynamic setConfig callback function
            to configure nImmediateRoamRssiDiff
            Usage: adb shell iwpriv wlan0 setConfig gImmediateRoamRssiDiff=[0 .. 125]
    \param  hHal - HAL handle for device
    \param  nImmediateRoamRssiDiff - minimum rssi difference between potential
            candidate and current AP.
    \param  sessionId - Session Identifier
    \- return Success or failure
    -------------------------------------------------------------------------*/

eHalStatus sme_UpdateImmediateRoamRssiDiff(tHalHandle hHal,
                                           v_U8_t nImmediateRoamRssiDiff,
                                           tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
               TRACE_CODE_SME_RX_HDD_UPDATE_RSSIDIFF, NO_SESSION, 0));
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set immediate roam rssi diff to %d - old value is %d - roam state is %s",
                     nImmediateRoamRssiDiff,
                     pMac->roam.configParam.nImmediateRoamRssiDiff,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pMac->roam.configParam.nImmediateRoamRssiDiff = nImmediateRoamRssiDiff;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_UpdateRoamRssiDiff
    \brief  Update RoamRssiDiff
            This function is called through dynamic setConfig callback function
            to configure RoamRssiDiff
            Usage: adb shell iwpriv wlan0 setConfig RoamRssiDiff=[0 .. 125]
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  RoamRssiDiff - minimum rssi difference between potential
            candidate and current AP.
    \- return Success or failure
    -------------------------------------------------------------------------*/

eHalStatus sme_UpdateRoamRssiDiff(tHalHandle hHal, tANI_U8 sessionId,
                                  v_U8_t RoamRssiDiff)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set roam rssi diff to %d - old value is %d - roam state is %s",
                     RoamRssiDiff,
                     pMac->roam.configParam.RoamRssiDiff,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pMac->roam.configParam.RoamRssiDiff = RoamRssiDiff;
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
       csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                          REASON_RSSI_DIFF_CHANGED);
    }
#endif
    return status ;
}

/*--------------------------------------------------------------------------
  \brief sme_UpdateFastTransitionEnabled() - enable/disable Fast Transition support at runtime
  It is used at in the REG_DYNAMIC_VARIABLE macro definition of
  isFastTransitionEnabled.
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return eHAL_STATUS_SUCCESS - SME update isFastTransitionEnabled config successfully.
          Other status means SME is failed to update isFastTransitionEnabled.
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_UpdateFastTransitionEnabled(tHalHandle hHal,
        v_BOOL_t isFastTransitionEnabled)
{
  tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
            TRACE_CODE_SME_RX_HDD_UPDATE_FTENABLED, NO_SESSION, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: FastTransitionEnabled is changed from %d to %d", __func__,
                      pMac->roam.configParam.isFastTransitionEnabled,
                      isFastTransitionEnabled);
        pMac->roam.configParam.isFastTransitionEnabled = isFastTransitionEnabled;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_UpdateWESMode
    \brief  Update WES Mode
            This function is called through dynamic setConfig callback function
            to configure isWESModeEnabled
    \param  hHal - HAL handle for device
    \param  isWESModeEnabled - WES mode
    \param  sessionId - Session Identifier
    \return eHAL_STATUS_SUCCESS - SME update isWESModeEnabled config successfully.
          Other status means SME is failed to update isWESModeEnabled.
    -------------------------------------------------------------------------*/

eHalStatus sme_UpdateWESMode(tHalHandle hHal, v_BOOL_t isWESModeEnabled,
                               tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set WES Mode to %d - old value is %d - roam state is %s",
                     isWESModeEnabled,
                     pMac->roam.configParam.isWESModeEnabled,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pMac->roam.configParam.isWESModeEnabled = isWESModeEnabled;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_SetRoamScanControl
    \brief  Set roam scan control
            This function is called to set roam scan control
            if roam scan control is set to 0, roaming scan cache is cleared
            any value other than 0 is treated as invalid value
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \return eHAL_STATUS_SUCCESS - SME update config successfully.
          Other status means SME failure to update
    -------------------------------------------------------------------------*/
eHalStatus sme_SetRoamScanControl(tHalHandle hHal, tANI_U8 sessionId,
                                  v_BOOL_t roamScanControl)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
             TRACE_CODE_SME_RX_HDD_SET_SCANCTRL, NO_SESSION, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                    "LFR runtime successfully set roam scan control to %d - old value is %d - roam state is %s",
                     roamScanControl,
                     pMac->roam.configParam.nRoamScanControl,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pMac->roam.configParam.nRoamScanControl = roamScanControl;
        if ( 0 == roamScanControl)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully cleared roam scan cache");
            csrFlushCfgBgScanRoamChannelList(pMac, sessionId);
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
           if (pMac->roam.configParam.isRoamOffloadScanEnabled)
           {
               csrRoamOffloadScan(pMac, sessionId,
                                  ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                                  REASON_FLUSH_CHANNEL_LIST);
           }
#endif
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return status ;
}
#endif /* (WLAN_FEATURE_VOWIFI_11R) || (FEATURE_WLAN_ESE) || (FEATURE_WLAN_LFR) */

#ifdef FEATURE_WLAN_LFR
/*--------------------------------------------------------------------------
  \brief sme_UpdateIsFastRoamIniFeatureEnabled() - enable/disable LFR support at runtime
  It is used at in the REG_DYNAMIC_VARIABLE macro definition of
  isFastRoamIniFeatureEnabled.
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  sessionId - Session Identifier
  \return eHAL_STATUS_SUCCESS - SME update isFastRoamIniFeatureEnabled config successfully.
          Other status means SME is failed to update isFastRoamIniFeatureEnabled.
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_UpdateIsFastRoamIniFeatureEnabled
(
    tHalHandle hHal,
    tANI_U8 sessionId,
    const v_BOOL_t isFastRoamIniFeatureEnabled)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    if (pMac->roam.configParam.isFastRoamIniFeatureEnabled ==
                  isFastRoamIniFeatureEnabled)
    {
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "%s: FastRoam is already enabled or disabled, nothing to do (returning) old(%d) new(%d)", __func__,
             pMac->roam.configParam.isFastRoamIniFeatureEnabled,
             isFastRoamIniFeatureEnabled);
        return eHAL_STATUS_SUCCESS;
    }

  VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "%s: FastRoamEnabled is changed from %d to %d", __func__,
           pMac->roam.configParam.isFastRoamIniFeatureEnabled,
           isFastRoamIniFeatureEnabled);
    pMac->roam.configParam.isFastRoamIniFeatureEnabled =
                                            isFastRoamIniFeatureEnabled;
    csrNeighborRoamUpdateFastRoamingEnabled(pMac, sessionId,
                                            isFastRoamIniFeatureEnabled);

  return eHAL_STATUS_SUCCESS;
}

/*--------------------------------------------------------------------------
  \brief sme_UpdateIsMAWCIniFeatureEnabled() -
  Enable/disable LFR MAWC support at runtime
  It is used at in the REG_DYNAMIC_VARIABLE macro definition of
  isMAWCIniFeatureEnabled.
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return eHAL_STATUS_SUCCESS - SME update MAWCEnabled config successfully.
          Other status means SME is failed to update MAWCEnabled.
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_UpdateIsMAWCIniFeatureEnabled(tHalHandle hHal,
        const v_BOOL_t MAWCEnabled)
{
  tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
  eHalStatus status = eHAL_STATUS_SUCCESS;

  status = sme_AcquireGlobalLock( &pMac->sme );
  if ( HAL_STATUS_SUCCESS( status ) )
  {
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                "%s: MAWCEnabled is changed from %d to %d", __func__,
                pMac->roam.configParam.MAWCEnabled,
                MAWCEnabled);
      pMac->roam.configParam.MAWCEnabled = MAWCEnabled;
      sme_ReleaseGlobalLock( &pMac->sme );
  }

  return status ;

}

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
/*--------------------------------------------------------------------------
  \brief sme_stopRoaming() - Stop roaming for a given sessionId
   This is a synchronous call
  \param hHal      - The handle returned by macOpen
  \param  sessionId - Session Identifier
  \return eHAL_STATUS_SUCCESS on success
          Other status on failure
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_stopRoaming(tHalHandle hHal,
                           tANI_U8 sessionId,
                           tANI_U8 reason)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus  status  = eHAL_STATUS_SUCCESS;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_STOP, reason);
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return status;
}

/*--------------------------------------------------------------------------
  \brief sme_startRoaming() - Start roaming for a given sessionId
   This is a synchronous call
  \param hHal      - The handle returned by macOpen
  \param  sessionId - Session Identifier
  \return eHAL_STATUS_SUCCESS on success
          Other status on failure
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_startRoaming(tHalHandle hHal,
                           tANI_U8 sessionId,
                           tANI_U8 reason)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus  status  = eHAL_STATUS_SUCCESS;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_START, reason);
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return status;
}

/*--------------------------------------------------------------------------
  \brief sme_UpdateEnableFastRoamInConcurrency() - enable/disable LFR if Concurrent session exists
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return eHAL_STATUS_SUCCESS
          Other status means SME is failed
  \sa
  --------------------------------------------------------------------------*/

eHalStatus sme_UpdateEnableFastRoamInConcurrency(tHalHandle hHal,
                          v_BOOL_t bFastRoamInConIniFeatureEnabled)
{

    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus  status  = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        pMac->roam.configParam.bFastRoamInConIniFeatureEnabled = bFastRoamInConIniFeatureEnabled;
        if (0 == pMac->roam.configParam.isRoamOffloadScanEnabled)
        {
            pMac->roam.configParam.bFastRoamInConIniFeatureEnabled = 0;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status;
}
#endif
#endif /* FEATURE_WLAN_LFR */

#ifdef FEATURE_WLAN_ESE
/*--------------------------------------------------------------------------
  \brief sme_UpdateIsEseFeatureEnabled() - enable/disable ESE support at runtime
  It is used at in the REG_DYNAMIC_VARIABLE macro definition of
  isEseIniFeatureEnabled.
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param sessionId - Session Identifier
  \param isEseIniFeatureEnabled - flag to enable/disable
  \return eHAL_STATUS_SUCCESS - SME update isEseIniFeatureEnabled config successfully.
          Other status means SME is failed to update isEseIniFeatureEnabled.
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_UpdateIsEseFeatureEnabled
(
    tHalHandle hHal,
    tANI_U8 sessionId,
    const v_BOOL_t isEseIniFeatureEnabled)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    if (pMac->roam.configParam.isEseIniFeatureEnabled == isEseIniFeatureEnabled)
    {
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: ESE Mode is already enabled or disabled, nothing to do (returning) old(%d) new(%d)", __func__,
                  pMac->roam.configParam.isEseIniFeatureEnabled,
                  isEseIniFeatureEnabled);
        return eHAL_STATUS_SUCCESS;
    }

  VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: EseEnabled is changed from %d to %d", __func__,
           pMac->roam.configParam.isEseIniFeatureEnabled,
           isEseIniFeatureEnabled);
    pMac->roam.configParam.isEseIniFeatureEnabled = isEseIniFeatureEnabled;
    csrNeighborRoamUpdateEseModeEnabled(pMac, sessionId,
                                        isEseIniFeatureEnabled);

    if (TRUE == isEseIniFeatureEnabled)
    {
        sme_UpdateFastTransitionEnabled(hHal, TRUE);
    }

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
       csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                          REASON_ESE_INI_CFG_CHANGED);
    }
#endif
    return eHAL_STATUS_SUCCESS;
}
#endif /* FEATURE_WLAN_ESE */

/*--------------------------------------------------------------------------
  \brief sme_UpdateConfigFwRssiMonitoring() - enable/disable firmware RSSI Monitoring at runtime
  It is used at in the REG_DYNAMIC_VARIABLE macro definition of
  fEnableFwRssiMonitoring.
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return eHAL_STATUS_SUCCESS - SME update fEnableFwRssiMonitoring. config successfully.
          Other status means SME is failed to update fEnableFwRssiMonitoring.
  \sa
  --------------------------------------------------------------------------*/

eHalStatus sme_UpdateConfigFwRssiMonitoring(tHalHandle hHal,
        v_BOOL_t fEnableFwRssiMonitoring)
{
    eHalStatus halStatus = eHAL_STATUS_SUCCESS;

    if (ccmCfgSetInt(hHal, WNI_CFG_PS_ENABLE_RSSI_MONITOR, fEnableFwRssiMonitoring,
                    NULL, eANI_BOOLEAN_FALSE)==eHAL_STATUS_FAILURE)
    {
        halStatus = eHAL_STATUS_FAILURE;
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "Failure: Could not pass on WNI_CFG_PS_RSSI_MONITOR configuration info to CCM");
    }

    return (halStatus);
}

#ifdef WLAN_FEATURE_NEIGHBOR_ROAMING
/* ---------------------------------------------------------------------------
    \fn     sme_SetRoamOpportunisticScanThresholdDiff
    \brief  Update Opportunistic Scan threshold diff
            This function is called through dynamic setConfig callback function
            to configure  nOpportunisticThresholdDiff
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nOpportunisticThresholdDiff - Opportunistic Scan threshold diff
    \return eHAL_STATUS_SUCCESS - SME update nOpportunisticThresholdDiff config
            successfully.
            else SME is failed to update nOpportunisticThresholdDiff.
    -------------------------------------------------------------------------*/
eHalStatus sme_SetRoamOpportunisticScanThresholdDiff(tHalHandle hHal,
                            tANI_U8 sessionId,
                            const v_U8_t nOpportunisticThresholdDiff)
{
    tpAniSirGlobal pMac    = PMAC_STRUCT( hHal );
    eHalStatus     status  = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrNeighborRoamSetOpportunisticScanThresholdDiff(pMac,
                                         sessionId,
                                         nOpportunisticThresholdDiff);
        if (HAL_STATUS_SUCCESS(status))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                "LFR runtime successfully set "
                "opportunistic threshold diff to %d"
                " - old value is %d - roam state is %d",
                nOpportunisticThresholdDiff,
                pMac->roam.configParam.neighborRoamConfig.nOpportunisticThresholdDiff,
                pMac->roam.neighborRoamInfo[sessionId].neighborRoamState);
            pMac->roam.configParam.neighborRoamConfig.nOpportunisticThresholdDiff = nOpportunisticThresholdDiff;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return status;
}

/*--------------------------------------------------------------------------
  \fn    sme_GetRoamOpportunisticScanThresholdDiff()
  \brief gets Opportunistic Scan threshold diff
         This is a synchronous call
  \param hHal - The handle returned by macOpen
  \return v_U8_t - nOpportunisticThresholdDiff
  \sa
  --------------------------------------------------------------------------*/
v_U8_t sme_GetRoamOpportunisticScanThresholdDiff(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.neighborRoamConfig.nOpportunisticThresholdDiff;
}

/* ---------------------------------------------------------------------------
    \fn     sme_SetRoamRescanRssiDiff
    \brief  Update roam rescan rssi diff
            This function is called through dynamic setConfig callback function
            to configure  nRoamRescanRssiDiff
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nRoamRescanRssiDiff - roam rescan rssi diff
    \return eHAL_STATUS_SUCCESS - SME update nRoamRescanRssiDiff config
            successfully.
            else SME is failed to update nRoamRescanRssiDiff.
    -------------------------------------------------------------------------*/
eHalStatus sme_SetRoamRescanRssiDiff(tHalHandle hHal,
                                     tANI_U8 sessionId,
                                     const v_U8_t nRoamRescanRssiDiff)
{
    tpAniSirGlobal pMac    = PMAC_STRUCT( hHal );
    eHalStatus     status  = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrNeighborRoamSetRoamRescanRssiDiff(pMac, sessionId,
        nRoamRescanRssiDiff);
        if (HAL_STATUS_SUCCESS(status))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                "LFR runtime successfully set "
                "opportunistic threshold diff to %d"
                " - old value is %d - roam state is %d",
                nRoamRescanRssiDiff,
                pMac->roam.configParam.neighborRoamConfig.nRoamRescanRssiDiff,
                pMac->roam.neighborRoamInfo[sessionId].neighborRoamState);
            pMac->roam.configParam.neighborRoamConfig.nRoamRescanRssiDiff =
                nRoamRescanRssiDiff;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/*--------------------------------------------------------------------------
  \fn    sme_GetRoamRescanRssiDiff
  \brief gets roam rescan rssi diff
         This is a synchronous call
  \param hHal - The handle returned by macOpen
  \return v_S7_t - nRoamRescanRssiDiff
  \sa
  --------------------------------------------------------------------------*/
v_U8_t sme_GetRoamRescanRssiDiff(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.neighborRoamConfig.nRoamRescanRssiDiff;
}

/* ---------------------------------------------------------------------------
    \fn     sme_SetRoamBmissFirstBcnt
    \brief  Update Roam count for first beacon miss
            This function is called through dynamic setConfig callback function
            to configure nRoamBmissFirstBcnt
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nRoamBmissFirstBcnt - Roam first bmiss count
    \return eHAL_STATUS_SUCCESS - SME update nRoamBmissFirstBcnt
            successfully.
            else SME is failed to update nRoamBmissFirstBcnt
    -------------------------------------------------------------------------*/
eHalStatus sme_SetRoamBmissFirstBcnt(tHalHandle hHal,
                                     tANI_U8 sessionId,
                                     const v_U8_t nRoamBmissFirstBcnt)
{
    tpAniSirGlobal pMac    = PMAC_STRUCT( hHal );
    eHalStatus     status  = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrNeighborRoamSetRoamBmissFirstBcnt(pMac, sessionId,
                                                      nRoamBmissFirstBcnt);
        if (HAL_STATUS_SUCCESS(status))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                "LFR runtime successfully set "
                "beacon miss first beacon count to %d"
                " - old value is %d - roam state is %d",
                nRoamBmissFirstBcnt,
                pMac->roam.configParam.neighborRoamConfig.nRoamBmissFirstBcnt,
                pMac->roam.neighborRoamInfo[sessionId].neighborRoamState);
            pMac->roam.configParam.neighborRoamConfig.nRoamBmissFirstBcnt =
                nRoamBmissFirstBcnt;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_GetRoamBmissFirstBcnt
    \brief  get neighbor roam beacon miss first count
    \param hHal - The handle returned by macOpen.
    \return v_U8_t - neighbor roam beacon miss first count
    -------------------------------------------------------------------------*/
v_U8_t sme_GetRoamBmissFirstBcnt(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.neighborRoamConfig.nRoamBmissFirstBcnt;
}

/* ---------------------------------------------------------------------------
    \fn     sme_SetRoamBmissFinalBcnt
    \brief  Update Roam count for final beacon miss
            This function is called through dynamic setConfig callback function
            to configure nRoamBmissFinalBcnt
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nRoamBmissFinalBcnt - Roam final bmiss count
    \return eHAL_STATUS_SUCCESS - SME update nRoamBmissFinalBcnt
            successfully.
            else SME is failed to update nRoamBmissFinalBcnt
    -------------------------------------------------------------------------*/
eHalStatus sme_SetRoamBmissFinalBcnt(tHalHandle hHal,
                                     tANI_U8 sessionId,
                                     const v_U8_t nRoamBmissFinalBcnt)
{
    tpAniSirGlobal pMac    = PMAC_STRUCT( hHal );
    eHalStatus     status  = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrNeighborRoamSetRoamBmissFinalBcnt(pMac, sessionId,
                                                      nRoamBmissFinalBcnt);
        if (HAL_STATUS_SUCCESS(status))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                "LFR runtime successfully set "
                "beacon miss final beacon count to %d"
                " - old value is %d - roam state is %d",
                nRoamBmissFinalBcnt,
                pMac->roam.configParam.neighborRoamConfig.nRoamBmissFinalBcnt,
                pMac->roam.neighborRoamInfo[sessionId].neighborRoamState);
            pMac->roam.configParam.neighborRoamConfig.nRoamBmissFinalBcnt =
                nRoamBmissFinalBcnt;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/*--------------------------------------------------------------------------
  \fn    sme_GetRoamBmissFinalBcnt
  \brief gets Roam count for final beacon miss
         This is a synchronous call
  \param hHal - The handle returned by macOpen
  \return v_U8_t - nRoamBmissFinalBcnt
  \sa
  --------------------------------------------------------------------------*/
v_U8_t sme_GetRoamBmissFinalBcnt(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.neighborRoamConfig.nRoamBmissFinalBcnt;
}

/* ---------------------------------------------------------------------------
    \fn     sme_SetRoamBeaconRssiWeight
    \brief  Update Roam beacon rssi weight
            This function is called through dynamic setConfig callback function
            to configure nRoamBeaconRssiWeight
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nRoamBeaconRssiWeight - Roam beacon rssi weight
    \return eHAL_STATUS_SUCCESS - SME update nRoamBeaconRssiWeight config
            successfully.
            else SME is failed to update nRoamBeaconRssiWeight
    -------------------------------------------------------------------------*/
eHalStatus sme_SetRoamBeaconRssiWeight(tHalHandle hHal,
                                       tANI_U8 sessionId,
                                     const v_U8_t nRoamBeaconRssiWeight)
{
    tpAniSirGlobal pMac    = PMAC_STRUCT( hHal );
    eHalStatus     status  = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrNeighborRoamSetRoamBeaconRssiWeight(pMac, sessionId,
                                                        nRoamBeaconRssiWeight);
        if (HAL_STATUS_SUCCESS(status))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                "LFR runtime successfully set "
                "beacon miss final beacon count to %d"
                " - old value is %d - roam state is %d",
                nRoamBeaconRssiWeight,
                pMac->roam.configParam.neighborRoamConfig.nRoamBeaconRssiWeight,
                pMac->roam.neighborRoamInfo[sessionId].neighborRoamState);
            pMac->roam.configParam.neighborRoamConfig.nRoamBeaconRssiWeight =
                nRoamBeaconRssiWeight;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/*--------------------------------------------------------------------------
  \fn    sme_GetRoamBeaconRssiWeight
  \brief gets Roam beacon rssi weight
         This is a synchronous call
  \param hHal - The handle returned by macOpen
  \return v_U8_t - nRoamBeaconRssiWeight
  \sa
  --------------------------------------------------------------------------*/
v_U8_t sme_GetRoamBeaconRssiWeight(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.neighborRoamConfig.nRoamBeaconRssiWeight;
}
/*--------------------------------------------------------------------------
  \brief sme_setNeighborLookupRssiThreshold() - update neighbor lookup rssi threshold
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  sessionId - Session Identifier
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_setNeighborLookupRssiThreshold
(
    tHalHandle hHal,
    tANI_U8 sessionId,
    v_U8_t neighborLookupRssiThreshold)
{
    tpAniSirGlobal pMac    = PMAC_STRUCT( hHal );
    eHalStatus     status  = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if (HAL_STATUS_SUCCESS(status))
    {
        status = csrNeighborRoamSetLookupRssiThreshold(pMac, sessionId,
                                                  neighborLookupRssiThreshold);
        if (HAL_STATUS_SUCCESS(status))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                      "LFR runtime successfully set Lookup threshold to %d - old value is %d - roam state is %s",
                      neighborLookupRssiThreshold,
                      pMac->roam.configParam.neighborRoamConfig.nNeighborLookupRssiThreshold,
                      macTraceGetNeighbourRoamState(
                      pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
            pMac->roam.configParam.neighborRoamConfig.nNeighborLookupRssiThreshold =
                                            neighborLookupRssiThreshold;
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return status;
}

/*--------------------------------------------------------------------------
  \brief sme_set_delay_before_vdev_stop() - update delay before VDEV_STOP
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  sessionId - Session Identifier
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_set_delay_before_vdev_stop(tHalHandle hHal,
                                          tANI_U8    sessionId,
                                          v_U8_t     delay_before_vdev_stop)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus     status  = eHAL_STATUS_SUCCESS;
    status = sme_AcquireGlobalLock( &pMac->sme );
    if (HAL_STATUS_SUCCESS(status))
    {
       VOS_TRACE(VOS_MODULE_ID_SME,
                 VOS_TRACE_LEVEL_DEBUG,
                 FL("LFR param delay_before_vdev_stop changed from %d to %d"),
              pMac->roam.configParam.neighborRoamConfig.delay_before_vdev_stop,
              delay_before_vdev_stop);

       pMac->roam.neighborRoamInfo[sessionId].cfgParams.delay_before_vdev_stop =
                                                         delay_before_vdev_stop;
       pMac->roam.configParam.neighborRoamConfig.delay_before_vdev_stop =
                                                         delay_before_vdev_stop;
       sme_ReleaseGlobalLock( &pMac->sme );
    }
    return eHAL_STATUS_SUCCESS;
}

/*--------------------------------------------------------------------------
  \brief sme_setNeighborReassocRssiThreshold() - update neighbor reassoc rssi threshold
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  sessionId - Session Identifier
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_setNeighborReassocRssiThreshold
(
    tHalHandle hHal,
    tANI_U8 sessionId,
    v_U8_t neighborReassocRssiThreshold)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tCsrNeighborRoamConfig *pNeighborRoamConfig = NULL;
    tpCsrNeighborRoamControlInfo pNeighborRoamInfo = NULL;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if (HAL_STATUS_SUCCESS(status))
    {
        pNeighborRoamConfig = &pMac->roam.configParam.neighborRoamConfig;
        pNeighborRoamInfo   = &pMac->roam.neighborRoamInfo[sessionId];
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set Reassoc threshold to %d- old value is %d - roam state is %s",
                     neighborReassocRssiThreshold,
                     pMac->roam.configParam.neighborRoamConfig.nNeighborReassocRssiThreshold,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pNeighborRoamConfig->nNeighborReassocRssiThreshold =
                                      neighborReassocRssiThreshold;
        pNeighborRoamInfo->cfgParams.neighborReassocThreshold =
                                      neighborReassocRssiThreshold;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;
}


/*--------------------------------------------------------------------------
  \brief sme_getNeighborLookupRssiThreshold() - get neighbor lookup rssi threshold
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
v_U8_t sme_getNeighborLookupRssiThreshold(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.neighborRoamConfig.nNeighborLookupRssiThreshold;
}

/*--------------------------------------------------------------------------
  \brief sme_setNeighborScanRefreshPeriod() - set neighbor scan results refresh period
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  sessionId - Session Identifier
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_setNeighborScanRefreshPeriod
(
    tHalHandle hHal,
    tANI_U8 sessionId,
    v_U16_t neighborScanResultsRefreshPeriod)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tCsrNeighborRoamConfig *pNeighborRoamConfig = NULL;
    tpCsrNeighborRoamControlInfo pNeighborRoamInfo = NULL;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        pNeighborRoamConfig = &pMac->roam.configParam.neighborRoamConfig;
        pNeighborRoamInfo   = &pMac->roam.neighborRoamInfo[sessionId];
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set roam scan refresh period to %d- old value is %d - roam state is %s",
                     neighborScanResultsRefreshPeriod,
                     pMac->roam.configParam.neighborRoamConfig.nNeighborResultsRefreshPeriod,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pNeighborRoamConfig->nNeighborResultsRefreshPeriod =
                                  neighborScanResultsRefreshPeriod;
        pNeighborRoamInfo->cfgParams.neighborResultsRefreshPeriod =
                                  neighborScanResultsRefreshPeriod;

        sme_ReleaseGlobalLock( &pMac->sme );
    }

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
       csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                          REASON_NEIGHBOR_SCAN_REFRESH_PERIOD_CHANGED);
    }
#endif
    return status ;
}

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
/*--------------------------------------------------------------------------
  \brief sme_UpdateRoamScanOffloadEnabled() -
         Enable/disable roam scan offload feature
  It is used at in the REG_DYNAMIC_VARIABLE macro definition of
  gRoamScanOffloadEnabled.
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return eHAL_STATUS_SUCCESS - SME update config successfully.
          Other status means SME is failed to update.
  \sa
  --------------------------------------------------------------------------*/

eHalStatus sme_UpdateRoamScanOffloadEnabled(tHalHandle hHal,
        v_BOOL_t nRoamScanOffloadEnabled)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
               FL("gRoamScanOffloadEnabled is changed from %d to %d"),
               pMac->roam.configParam.isRoamOffloadScanEnabled,
               nRoamScanOffloadEnabled);
        pMac->roam.configParam.isRoamOffloadScanEnabled =
                                           nRoamScanOffloadEnabled;
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return status ;
}
#endif

/*--------------------------------------------------------------------------
  \brief sme_getNeighborScanRefreshPeriod() - get neighbor scan results refresh period
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return v_U16_t - Neighbor scan results refresh period value
  \sa
  --------------------------------------------------------------------------*/
v_U16_t sme_getNeighborScanRefreshPeriod(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.neighborRoamConfig.nNeighborResultsRefreshPeriod;
}

/*--------------------------------------------------------------------------
  \brief sme_getEmptyScanRefreshPeriod() - get empty scan refresh period
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
v_U16_t sme_getEmptyScanRefreshPeriod(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.neighborRoamConfig.nEmptyScanRefreshPeriod;
}

/* ---------------------------------------------------------------------------
    \fn sme_UpdateEmptyScanRefreshPeriod
    \brief  Update nEmptyScanRefreshPeriod
            This function is called through dynamic setConfig callback function
            to configure nEmptyScanRefreshPeriod
            Usage: adb shell iwpriv wlan0 setConfig nEmptyScanRefreshPeriod=[0 .. 60]
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nEmptyScanRefreshPeriod - scan period following empty scan results.
    \- return Success or failure
    -------------------------------------------------------------------------*/

eHalStatus sme_UpdateEmptyScanRefreshPeriod(tHalHandle hHal, tANI_U8 sessionId,
                                            v_U16_t nEmptyScanRefreshPeriod)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tCsrNeighborRoamConfig *pNeighborRoamConfig = NULL;
    tpCsrNeighborRoamControlInfo pNeighborRoamInfo = NULL;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        pNeighborRoamConfig = &pMac->roam.configParam.neighborRoamConfig;
        pNeighborRoamInfo   = &pMac->roam.neighborRoamInfo[sessionId];
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set roam scan period to %d -old value is %d - roam state is %s",
                     nEmptyScanRefreshPeriod,
                     pMac->roam.configParam.neighborRoamConfig.nEmptyScanRefreshPeriod,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pNeighborRoamConfig->nEmptyScanRefreshPeriod = nEmptyScanRefreshPeriod;
        pNeighborRoamInfo->cfgParams.emptyScanRefreshPeriod =
                                             nEmptyScanRefreshPeriod;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
       csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                          REASON_EMPTY_SCAN_REF_PERIOD_CHANGED);
    }
#endif
    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_setNeighborScanMinChanTime
    \brief  Update nNeighborScanMinChanTime
            This function is called through dynamic setConfig callback function
            to configure gNeighborScanChannelMinTime
            Usage: adb shell iwpriv wlan0 setConfig gNeighborScanChannelMinTime=[0 .. 60]
    \param  hHal - HAL handle for device
    \param  nNeighborScanMinChanTime - Channel minimum dwell time
    \param  sessionId - Session Identifier
    \- return Success or failure
    -------------------------------------------------------------------------*/
eHalStatus sme_setNeighborScanMinChanTime(tHalHandle hHal,
                                        const v_U16_t nNeighborScanMinChanTime,
                                        tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set channel min dwell time to %d - old value is %d - roam state is %s",
                     nNeighborScanMinChanTime,
                     pMac->roam.configParam.neighborRoamConfig.nNeighborScanMinChanTime,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));

        pMac->roam.configParam.neighborRoamConfig.nNeighborScanMinChanTime =
                                                       nNeighborScanMinChanTime;
        pMac->roam.neighborRoamInfo[sessionId].cfgParams.minChannelScanTime =
                                                       nNeighborScanMinChanTime;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_setNeighborScanMaxChanTime
    \brief  Update nNeighborScanMaxChanTime
            This function is called through dynamic setConfig callback function
            to configure gNeighborScanChannelMaxTime
            Usage: adb shell iwpriv wlan0 setConfig gNeighborScanChannelMaxTime=[0 .. 60]
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nNeighborScanMinChanTime - Channel maximum dwell time
    \- return Success or failure
    -------------------------------------------------------------------------*/
eHalStatus sme_setNeighborScanMaxChanTime(tHalHandle hHal, tANI_U8 sessionId,
                                        const v_U16_t nNeighborScanMaxChanTime)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tCsrNeighborRoamConfig *pNeighborRoamConfig = NULL;
    tpCsrNeighborRoamControlInfo pNeighborRoamInfo = NULL;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        pNeighborRoamConfig = &pMac->roam.configParam.neighborRoamConfig;
        pNeighborRoamInfo   = &pMac->roam.neighborRoamInfo[sessionId];
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set channel max dwell time to %d - old value is %d - roam state is %s",
                     nNeighborScanMaxChanTime,
                     pMac->roam.configParam.neighborRoamConfig.nNeighborScanMaxChanTime,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pNeighborRoamConfig->nNeighborScanMaxChanTime =
                                                  nNeighborScanMaxChanTime;
        pNeighborRoamInfo->cfgParams.maxChannelScanTime =
                                                  nNeighborScanMaxChanTime;
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
       csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                          REASON_SCAN_CH_TIME_CHANGED);
    }
#endif

    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_getNeighborScanMinChanTime
    \brief  get neighbor scan min channel time
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_U16_t - channel min time value
    -------------------------------------------------------------------------*/
v_U16_t sme_getNeighborScanMinChanTime(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.neighborRoamInfo[sessionId].cfgParams.minChannelScanTime;
}

/* ---------------------------------------------------------------------------
    \fn sme_getNeighborRoamState
    \brief  get neighbor roam state
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_U32_t - neighbor roam state
    -------------------------------------------------------------------------*/
v_U32_t sme_getNeighborRoamState(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    return pMac->roam.neighborRoamInfo[sessionId].neighborRoamState;
}

/* ---------------------------------------------------------------------------
    \fn sme_getCurrentRoamState
    \brief  get current roam state
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_U32_t - current roam state
    -------------------------------------------------------------------------*/
v_U32_t sme_getCurrentRoamState(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    return pMac->roam.curState[sessionId];
}

/* ---------------------------------------------------------------------------
    \fn sme_getCurrentRoamSubState
    \brief  get neighbor roam sub state
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_U32_t - current roam sub state
    -------------------------------------------------------------------------*/
v_U32_t sme_getCurrentRoamSubState(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    return pMac->roam.curSubState[sessionId];
}

/* ---------------------------------------------------------------------------
    \fn sme_getLimSmeState
    \brief  get Lim Sme state
    \param hHal - The handle returned by macOpen.
    \return v_U32_t - Lim Sme state
    -------------------------------------------------------------------------*/
v_U32_t sme_getLimSmeState(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    return pMac->lim.gLimSmeState;
}

/* ---------------------------------------------------------------------------
    \fn sme_getLimMlmState
    \brief  get Lim Mlm state
    \param hHal - The handle returned by macOpen.
    \return v_U32_t - Lim Mlm state
    -------------------------------------------------------------------------*/
v_U32_t sme_getLimMlmState(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    return pMac->lim.gLimMlmState;
}

/* ---------------------------------------------------------------------------
    \fn sme_IsLimSessionValid
    \brief  is Lim session valid
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_BOOL_t - true or false
    -------------------------------------------------------------------------*/
v_BOOL_t sme_IsLimSessionValid(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    if (sessionId > pMac->lim.maxBssId) {
        smsLog(pMac, LOG1, FL("invalid sessionId:%d"), sessionId);
        return FALSE;
    }

    return pMac->lim.gpSession[sessionId].valid;
}

/* ---------------------------------------------------------------------------
    \fn sme_getLimSmeSessionState
    \brief  get Lim Sme session state
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_U32_t - Lim Sme session state
    -------------------------------------------------------------------------*/
v_U32_t sme_getLimSmeSessionState(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    return pMac->lim.gpSession[sessionId].limSmeState;
}

/* ---------------------------------------------------------------------------
    \fn sme_getLimMlmSessionState
    \brief  get Lim Mlm session state
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_U32_t - Lim Mlm session state
    -------------------------------------------------------------------------*/
v_U32_t sme_getLimMlmSessionState(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    return pMac->lim.gpSession[sessionId].limMlmState;
}


/* ---------------------------------------------------------------------------
    \fn sme_getNeighborScanMaxChanTime
    \brief  get neighbor scan max channel time
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_U16_t - channel max time value
    -------------------------------------------------------------------------*/
v_U16_t sme_getNeighborScanMaxChanTime(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.neighborRoamInfo[sessionId].cfgParams.maxChannelScanTime;
}

/* ---------------------------------------------------------------------------
    \fn sme_setNeighborScanPeriod
    \brief  Update nNeighborScanPeriod
            This function is called through dynamic setConfig callback function
            to configure nNeighborScanPeriod
            Usage: adb shell iwpriv wlan0 setConfig nNeighborScanPeriod=[0 .. 1000]
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  nNeighborScanPeriod - neighbor scan period
    \- return Success or failure
    -------------------------------------------------------------------------*/
eHalStatus sme_setNeighborScanPeriod(tHalHandle hHal, tANI_U8 sessionId,
                                     const v_U16_t nNeighborScanPeriod)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tCsrNeighborRoamConfig *pNeighborRoamConfig = NULL;
    tpCsrNeighborRoamControlInfo pNeighborRoamInfo = NULL;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        pNeighborRoamConfig = &pMac->roam.configParam.neighborRoamConfig;
        pNeighborRoamInfo   = &pMac->roam.neighborRoamInfo[sessionId];
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set neighbor scan period to %d"
                     " - old value is %d - roam state is %s",
                     nNeighborScanPeriod,
                     pMac->roam.configParam.neighborRoamConfig.nNeighborScanTimerPeriod,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pNeighborRoamConfig->nNeighborScanTimerPeriod = nNeighborScanPeriod;
        pNeighborRoamInfo->cfgParams.neighborScanPeriod = nNeighborScanPeriod;
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
       csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                          REASON_SCAN_HOME_TIME_CHANGED);
    }
#endif

    return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_getNeighborScanPeriod
    \brief  get neighbor scan period
    \param hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return v_U16_t - neighbor scan period
    -------------------------------------------------------------------------*/
v_U16_t sme_getNeighborScanPeriod(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.neighborRoamInfo[sessionId].cfgParams.neighborScanPeriod;
}

#endif

#if  defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)

/*--------------------------------------------------------------------------
  \brief sme_getRoamRssiDiff() - get Roam rssi diff
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return v_U16_t - Rssi diff value
  \sa
  --------------------------------------------------------------------------*/
v_U8_t sme_getRoamRssiDiff(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.RoamRssiDiff;
}

/*--------------------------------------------------------------------------
  \brief sme_ChangeRoamScanChannelList() - Change roam scan channel list
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  sessionId - Session Identifier
  \param  pChannelList - Output channel list
  \param  numChannels - Output number of channels
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_ChangeRoamScanChannelList(tHalHandle hHal, tANI_U8 sessionId,
                                         tANI_U8 *pChannelList,
                                         tANI_U8 numChannels)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tpCsrNeighborRoamControlInfo pNeighborRoamInfo =
                                        &pMac->roam.neighborRoamInfo[sessionId];
    tANI_U8 oldChannelList[WNI_CFG_VALID_CHANNEL_LIST_LEN*2] = {0};
    tANI_U8 newChannelList[WNI_CFG_VALID_CHANNEL_LIST_LEN*2] = {0};
    tANI_U8 i = 0, j = 0;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        if (NULL != pNeighborRoamInfo->cfgParams.channelInfo.ChannelList)
        {
            for (i = 0; i < pNeighborRoamInfo->cfgParams.channelInfo.numOfChannels; i++)
            {
                if (j < sizeof(oldChannelList))
                {
                    j += snprintf(oldChannelList + j, sizeof(oldChannelList) - j," %d",
                    pNeighborRoamInfo->cfgParams.channelInfo.ChannelList[i]);
                }
                else
                {
                    break;
                }
            }
        }
        csrFlushCfgBgScanRoamChannelList(pMac, sessionId);
        csrCreateBgScanRoamChannelList(pMac, sessionId, pChannelList,
                                       numChannels);
        sme_SetRoamScanControl(hHal, sessionId, 1);
        if (NULL != pNeighborRoamInfo->cfgParams.channelInfo.ChannelList)
        {
            j = 0;
            for (i = 0; i < pNeighborRoamInfo->cfgParams.channelInfo.numOfChannels; i++)
            {
                if (j < sizeof(newChannelList))
                {
                    j += snprintf(newChannelList + j, sizeof(newChannelList) - j," %d",
                           pNeighborRoamInfo->cfgParams.channelInfo.ChannelList[i]);
                }
                else
                {
                    break;
                }
            }
        }
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                  "LFR runtime successfully set roam scan channels to %s - old value is %s - roam state is %d",
                  newChannelList, oldChannelList,
                  pMac->roam.neighborRoamInfo[sessionId].neighborRoamState);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
       csrRoamOffloadScan(pMac, sessionId,
                          ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                          REASON_CHANNEL_LIST_CHANGED);
    }
#endif

    return status ;
}

#ifdef FEATURE_WLAN_ESE_UPLOAD
/*--------------------------------------------------------------------------
  \brief sme_SetEseRoamScanChannelList() - set ese roam scan channel list
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  sessionId - Session Identifier
  \param  pChannelList - Input channel list
  \param  numChannels - Input number of channels
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_SetEseRoamScanChannelList(tHalHandle hHal,
                                         tANI_U8 sessionId,
                                         tANI_U8 *pChannelList,
                                         tANI_U8 numChannels)
{
    tpAniSirGlobal      pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tpCsrNeighborRoamControlInfo    pNeighborRoamInfo
       = &pMac->roam.neighborRoamInfo[sessionId];
    tpCsrChannelInfo    currChannelListInfo
       = &pNeighborRoamInfo->roamChannelInfo.currentChannelListInfo;
    tANI_U8             oldChannelList[WNI_CFG_VALID_CHANNEL_LIST_LEN*2] = {0};
    tANI_U8             newChannelList[128] = {0};
    tANI_U8             i = 0, j = 0;
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        if (NULL != currChannelListInfo->ChannelList)
        {
            for (i = 0; i < currChannelListInfo->numOfChannels; i++)
            {
                j += snprintf(oldChannelList + j,
                              sizeof(oldChannelList) - j,
                              " %d",
                              currChannelListInfo->ChannelList[i]);
            }
        }
        status = csrCreateRoamScanChannelList(pMac, sessionId, pChannelList,
                   numChannels, csrGetCurrentBand(hHal));
        if ( HAL_STATUS_SUCCESS( status ))
        {
            if (NULL != currChannelListInfo->ChannelList)
            {
                j = 0;
                for (i = 0; i < currChannelListInfo->numOfChannels; i++)
                {
                    j += snprintf(newChannelList + j,
                                  sizeof(newChannelList) - j,
                                  " %d",
                    currChannelListInfo->ChannelList[i]);
                }
            }
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
"ESE roam scan channel list successfully set to \
%s - old value is %s - roam state is %d",
                      newChannelList, oldChannelList,
                      pNeighborRoamInfo->neighborRoamState);
        }
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
        if (pMac->roam.configParam.isRoamOffloadScanEnabled)
        {
           csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                              REASON_CHANNEL_LIST_CHANGED);
        }
#endif
    return status ;
}
#endif

/*--------------------------------------------------------------------------
  \brief sme_getRoamScanChannelList() - get roam scan channel list
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  pChannelList - Output channel list
  \param  numChannels - Output number of channels
  \param  sessionId - Session Identifier
  \return eHAL_STATUS_SUCCESS - SME update config successful.
          Other status means SME is failed to update
  \sa
  --------------------------------------------------------------------------*/
eHalStatus sme_getRoamScanChannelList(tHalHandle hHal, tANI_U8 *pChannelList,
                                     tANI_U8 *pNumChannels, tANI_U8 sessionId)
{
    int i  = 0;
    tANI_U8 *pOutPtr = pChannelList;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    tpCsrNeighborRoamControlInfo pNeighborRoamInfo =
                                        &pMac->roam.neighborRoamInfo[sessionId];
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        if (NULL == pNeighborRoamInfo->cfgParams.channelInfo.ChannelList)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_WARN,
                     "Roam Scan channel list is NOT yet initialized");
            *pNumChannels = 0;
            sme_ReleaseGlobalLock( &pMac->sme );
            return status;
        }

        *pNumChannels = pNeighborRoamInfo->cfgParams.channelInfo.numOfChannels;
        for (i = 0; i < (*pNumChannels); i++)
        {
            pOutPtr[i] = pNeighborRoamInfo->cfgParams.channelInfo.ChannelList[i];
        }
        pOutPtr[i] = '\0';
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return status ;
}

/*--------------------------------------------------------------------------
  \brief sme_getIsEseFeatureEnabled() - get ESE feature enabled or not
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return TRUE (1) - if the ESE feature is enabled
          FALSE (0) - if feature is disabled (compile or runtime)
  \sa
  --------------------------------------------------------------------------*/
tANI_BOOLEAN sme_getIsEseFeatureEnabled(tHalHandle hHal)
{
#ifdef FEATURE_WLAN_ESE
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return csrRoamIsEseIniFeatureEnabled(pMac);
#else
    return eANI_BOOLEAN_FALSE;
#endif
}

/*--------------------------------------------------------------------------
  \brief sme_GetWESMode() - get WES Mode
  This is a synchronous call
  \param hHal - The handle returned by macOpen
  \return v_U8_t - WES Mode Enabled(1)/Disabled(0)
  \sa
  --------------------------------------------------------------------------*/
v_BOOL_t sme_GetWESMode(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.isWESModeEnabled;
}

/*--------------------------------------------------------------------------
  \brief sme_GetRoamScanControl() - get scan control
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return v_BOOL_t - Enabled(1)/Disabled(0)
  \sa
  --------------------------------------------------------------------------*/
v_BOOL_t sme_GetRoamScanControl(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.nRoamScanControl;
}
#endif

/*--------------------------------------------------------------------------
  \brief sme_getIsLfrFeatureEnabled() - get LFR feature enabled or not
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return TRUE (1) - if the feature is enabled
          FALSE (0) - if feature is disabled (compile or runtime)
  \sa
  --------------------------------------------------------------------------*/
tANI_BOOLEAN sme_getIsLfrFeatureEnabled(tHalHandle hHal)
{
#ifdef FEATURE_WLAN_LFR
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.isFastRoamIniFeatureEnabled;
#else
    return eANI_BOOLEAN_FALSE;
#endif
}

/*--------------------------------------------------------------------------
  \brief sme_getIsFtFeatureEnabled() - get FT feature enabled or not
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return TRUE (1) - if the feature is enabled
          FALSE (0) - if feature is disabled (compile or runtime)
  \sa
  --------------------------------------------------------------------------*/
tANI_BOOLEAN sme_getIsFtFeatureEnabled(tHalHandle hHal)
{
#ifdef WLAN_FEATURE_VOWIFI_11R
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.isFastTransitionEnabled;
#else
    return eANI_BOOLEAN_FALSE;
#endif
}


/* ---------------------------------------------------------------------------
    \fn sme_IsFeatureSupportedByFW
    \brief  Check if an feature is enabled by FW

    \param  feattEnumValue - Enumeration value from placeHolderInCapBitmap
    \- return 1/0 (TRUE/FALSE)
    -------------------------------------------------------------------------*/
tANI_U8 sme_IsFeatureSupportedByFW(tANI_U8 featEnumValue)
{
   return IS_FEATURE_SUPPORTED_BY_FW(featEnumValue);
}
#ifdef FEATURE_WLAN_TDLS

/* ---------------------------------------------------------------------------
    \fn sme_SendTdlsLinkEstablishParams
    \brief  API to send TDLS Peer Link Establishment Parameters.

    \param  peerMac - peer's Mac Address.
    \param  tdlsLinkEstablishParams - TDLS Peer Link Establishment Parameters
    \- return VOS_STATUS_SUCCES
    -------------------------------------------------------------------------*/
VOS_STATUS sme_SendTdlsLinkEstablishParams(tHalHandle hHal,
                                           tANI_U8 sessionId,
                                           const tSirMacAddr peerMac,
                                           tCsrTdlsLinkEstablishParams *tdlsLinkEstablishParams)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                   TRACE_CODE_SME_RX_HDD_TDLS_LINK_ESTABLISH_PARAM,
                   sessionId, tdlsLinkEstablishParams->isOffChannelSupported));
    status = sme_AcquireGlobalLock( &pMac->sme );

    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrTdlsSendLinkEstablishParams(hHal, sessionId, peerMac, tdlsLinkEstablishParams) ;
        sme_ReleaseGlobalLock( &pMac->sme );
    }
   return status ;
}

/* ---------------------------------------------------------------------------
    \fn sme_SendTdlsMgmtFrame
    \brief  API to send TDLS management frames.

    \param  peerMac - peer's Mac Address.
    \param frame_type - Type of TDLS mgmt frame to be sent.
    \param dialog - dialog token used in the frame.
    \param status - status to be included in the frame.
    \param peerCapability - peer capabilities
    \param buf - additional IEs to be included
    \param len - length of additional Ies
    \param responder - Tdls request type
    \- return VOS_STATUS_SUCCES
    -------------------------------------------------------------------------*/
VOS_STATUS sme_SendTdlsMgmtFrame(tHalHandle hHal, tANI_U8 sessionId,
                                 const tSirMacAddr peerMac, tANI_U8 frame_type,
                                 tANI_U8 dialog, tANI_U16 statusCode,
                                 tANI_U32 peerCapability, tANI_U8 *buf,
                                 tANI_U8 len, tANI_U8 responder)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tCsrTdlsSendMgmt sendTdlsReq = {{0}} ;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_TDLS_SEND_MGMT_FRAME,
                     sessionId, statusCode));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        vos_mem_copy(sendTdlsReq.peerMac, peerMac, sizeof(tSirMacAddr)) ;
        sendTdlsReq.frameType = frame_type;
        sendTdlsReq.buf = buf;
        sendTdlsReq.len = len;
        sendTdlsReq.dialog = dialog;
        sendTdlsReq.statusCode = statusCode;
        sendTdlsReq.responder = responder;
        sendTdlsReq.peerCapability = peerCapability;

        status = csrTdlsSendMgmtReq(hHal, sessionId, &sendTdlsReq) ;

        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;

}
/* ---------------------------------------------------------------------------
    \fn sme_ChangeTdlsPeerSta
    \brief  API to Update TDLS peer sta parameters.

    \param  peerMac - peer's Mac Address.
    \param  staParams - Peer Station Parameters
    \- return VOS_STATUS_SUCCES
    -------------------------------------------------------------------------*/
VOS_STATUS sme_ChangeTdlsPeerSta(tHalHandle hHal, tANI_U8 sessionId,
                                 const tSirMacAddr peerMac,
                                 tCsrStaParams *pstaParams)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);

    if (NULL == pstaParams)
    {
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s :pstaParams is NULL",__func__);
        return eHAL_STATUS_FAILURE;
    }
    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_TDLS_CHANGE_PEER_STA, sessionId,
                     pstaParams->capability));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrTdlsChangePeerSta(hHal, sessionId, peerMac, pstaParams);

        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;

}

/* ---------------------------------------------------------------------------
    \fn sme_AddTdlsPeerSta
    \brief  API to Add TDLS peer sta entry.

    \param  peerMac - peer's Mac Address.
    \- return VOS_STATUS_SUCCES
    -------------------------------------------------------------------------*/
VOS_STATUS sme_AddTdlsPeerSta(tHalHandle hHal, tANI_U8 sessionId,
                              const tSirMacAddr peerMac)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_TDLS_ADD_PEER_STA,
                     sessionId, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrTdlsAddPeerSta(hHal, sessionId, peerMac);

        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;

}
/* ---------------------------------------------------------------------------
    \fn sme_DeleteTdlsPeerSta
    \brief  API to Delete TDLS peer sta entry.

    \param  peerMac - peer's Mac Address.
    \- return VOS_STATUS_SUCCES
    -------------------------------------------------------------------------*/
VOS_STATUS sme_DeleteTdlsPeerSta(tHalHandle hHal,
                                 tANI_U8 sessionId,
                                 const tSirMacAddr peerMac)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_TDLS_DEL_PEER_STA,
                     sessionId, 0));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrTdlsDelPeerSta(hHal, sessionId, peerMac) ;
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;

}
/* ---------------------------------------------------------------------------
    \fn sme_SetTdlsPowerSaveProhibited
    \API to set/reset the isTdlsPowerSaveProhibited.

    \- return void
    -------------------------------------------------------------------------*/
void sme_SetTdlsPowerSaveProhibited(tHalHandle hHal, tANI_U32 sessionId, v_BOOL_t val)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    if (!pMac->psOffloadEnabled)
    {
        pMac->isTdlsPowerSaveProhibited = val;
    }
    else
    {
        pmcOffloadSetTdlsProhibitBmpsStatus(hHal, sessionId, val);
    }
    smsLog(pMac, LOG1, FL("isTdlsPowerSaveProhibited is %d"),
                   pMac->isTdlsPowerSaveProhibited);
    return;
}

/* ---------------------------------------------------------------------------
  \fn    sme_UpdateFwTdlsState

  \brief
    SME will send message to WMA to set TDLS state in f/w

  \param

    hHal - The handle returned by macOpen

    psmeTdlsParams - TDLS state info to update in f/w

    useSmeLock - Need to acquire SME Global Lock before state update or not

  \return eHalStatus
--------------------------------------------------------------------------- */
eHalStatus sme_UpdateFwTdlsState(tHalHandle hHal, void  *psmeTdlsParams,
                                 tANI_BOOLEAN useSmeLock)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = NULL;
    vos_msg_t vosMessage;

    /* only acquire sme global lock before state update if asked to */
    if (useSmeLock) {
        pMac = PMAC_STRUCT(hHal);
        if (NULL == pMac)
            return eHAL_STATUS_FAILURE;

        status = sme_AcquireGlobalLock(&pMac->sme);
        if (eHAL_STATUS_SUCCESS != status)
            return status;
    }

    /* serialize the req through MC thread */
    vosMessage.bodyptr = psmeTdlsParams;
    vosMessage.type    = WDA_UPDATE_FW_TDLS_STATE;
    vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
    if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        status = eHAL_STATUS_FAILURE;

    /* release the lock if it was acquired */
    if (useSmeLock)
        sme_ReleaseGlobalLock(&pMac->sme);

    return(status);
}

/* ---------------------------------------------------------------------------
  \fn    sme_UpdateTdlsPeerState

  \brief
    SME will send message to WMA to set TDLS Peer state in f/w

  \param

    hHal - The handle returned by macOpen

    peerStateParams - TDLS Peer state info to update in f/w

  \return eHalStatus
--------------------------------------------------------------------------- */
eHalStatus sme_UpdateTdlsPeerState(tHalHandle hHal,
                                   tSmeTdlsPeerStateParams *peerStateParams)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tTdlsPeerStateParams *pTdlsPeerStateParams = NULL;
    vos_msg_t vosMessage;
    tANI_U8 num;
    tANI_U8 chanId;
    tANI_U8 i;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme)))
    {
        pTdlsPeerStateParams = vos_mem_malloc(sizeof(*pTdlsPeerStateParams));
        if (NULL == pTdlsPeerStateParams)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      "%s: failed to allocate mem for tdls peer state param",
                      __func__);
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }

        vos_mem_zero(pTdlsPeerStateParams, sizeof(*pTdlsPeerStateParams));
        vos_mem_copy(&pTdlsPeerStateParams->peerMacAddr,
                     &peerStateParams->peerMacAddr,
                     sizeof(tSirMacAddr));
        pTdlsPeerStateParams->vdevId = peerStateParams->vdevId;
        pTdlsPeerStateParams->peerState = peerStateParams->peerState;

        switch (peerStateParams->peerState)
        {
           case eSME_TDLS_PEER_STATE_PEERING:
              pTdlsPeerStateParams->peerState = WDA_TDLS_PEER_STATE_PEERING;
              break;

           case eSME_TDLS_PEER_STATE_CONNECTED:
              pTdlsPeerStateParams->peerState = WDA_TDLS_PEER_STATE_CONNECTED;
              break;

           case eSME_TDLS_PEER_STATE_TEARDOWN:
              pTdlsPeerStateParams->peerState = WDA_TDLS_PEER_STATE_TEARDOWN;
              break;

           case eSME_TDLS_PEER_ADD_MAC_ADDR:
              pTdlsPeerStateParams->peerState = WDA_TDLS_PEER_ADD_MAC_ADDR;
              break;

           case eSME_TDLS_PEER_REMOVE_MAC_ADDR:
              pTdlsPeerStateParams->peerState = WDA_TDLS_PEER_REMOVE_MAC_ADDR;
              break;

           default:
              VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                        "%s: invalid peer state param (%d)",
                        __func__, peerStateParams->peerState);
              vos_mem_free(pTdlsPeerStateParams);
              sme_ReleaseGlobalLock(&pMac->sme);
              return eHAL_STATUS_FAILURE;
       }

       pTdlsPeerStateParams->peerCap.isPeerResponder =
           peerStateParams->peerCap.isPeerResponder;
       pTdlsPeerStateParams->peerCap.peerUapsdQueue =
           peerStateParams->peerCap.peerUapsdQueue;
       pTdlsPeerStateParams->peerCap.peerMaxSp =
           peerStateParams->peerCap.peerMaxSp;
       pTdlsPeerStateParams->peerCap.peerBuffStaSupport =
           peerStateParams->peerCap.peerBuffStaSupport;
       pTdlsPeerStateParams->peerCap.peerOffChanSupport =
           peerStateParams->peerCap.peerOffChanSupport;
       pTdlsPeerStateParams->peerCap.peerCurrOperClass =
           peerStateParams->peerCap.peerCurrOperClass;
       pTdlsPeerStateParams->peerCap.selfCurrOperClass =
           peerStateParams->peerCap.selfCurrOperClass;

       num = 0;
       for (i = 0; i < peerStateParams->peerCap.peerChanLen; i++)
       {
           chanId = peerStateParams->peerCap.peerChan[i];
           if (csrRoamIsChannelValid(pMac, chanId))
           {
               pTdlsPeerStateParams->peerCap.peerChan[num].chanId = chanId;
               pTdlsPeerStateParams->peerCap.peerChan[num].pwr =
                                         csrGetCfgMaxTxPower(pMac, chanId);

               if (vos_nv_getChannelEnabledState(chanId) == NV_CHANNEL_DFS)
                   continue;
               else
               {
                   pTdlsPeerStateParams->peerCap.peerChan[num].dfsSet =
                                                                  VOS_FALSE;
               }

               if (vos_nv_skip_dsrc_dfs_2g(chanId, NV_CHANNEL_SKIP_DSRC))
                   continue;

               num++;
           }
       }
       pTdlsPeerStateParams->peerCap.peerChanLen = num;

       pTdlsPeerStateParams->peerCap.peerOperClassLen =
           peerStateParams->peerCap.peerOperClassLen;
       for (i = 0; i < HAL_TDLS_MAX_SUPP_OPER_CLASSES; i++)
       {
           pTdlsPeerStateParams->peerCap.peerOperClass[i] =
               peerStateParams->peerCap.peerOperClass[i];
       }

       pTdlsPeerStateParams->peerCap.prefOffChanNum =
           peerStateParams->peerCap.prefOffChanNum;
       pTdlsPeerStateParams->peerCap.prefOffChanBandwidth =
           peerStateParams->peerCap.prefOffChanBandwidth;
       pTdlsPeerStateParams->peerCap.opClassForPrefOffChan =
           peerStateParams->peerCap.opClassForPrefOffChan;

       vosMessage.type = WDA_UPDATE_TDLS_PEER_STATE;
       vosMessage.reserved = 0;
       vosMessage.bodyptr = pTdlsPeerStateParams;

       vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
       if (!VOS_IS_STATUS_SUCCESS(vosStatus))
       {
          vos_mem_free(pTdlsPeerStateParams);
          status = eHAL_STATUS_FAILURE;
       }

       sme_ReleaseGlobalLock(&pMac->sme);
    }
    return(status);
}

/* ---------------------------------------------------------------------------
    \fn sme_SendTdlsChanSwitchReq
    \brief  API to send TDLS channel switch parameters to WMA.

    \param  hHal - Umac handle.
    \param  chanSwitchParams-  vdev, channel, offset, mode, peerMac
    \- return VOS_STATUS_SUCCES
    -------------------------------------------------------------------------*/
eHalStatus sme_SendTdlsChanSwitchReq(tHalHandle hHal,
                                     tSmeTdlsChanSwitchParams *chanSwitchParams)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tTdlsChanSwitchParams *pTdlsChanSwitchParams = NULL;
    vos_msg_t vosMessage;

    MTRACE(vos_trace(VOS_MODULE_ID_SME,
                     TRACE_CODE_SME_RX_HDD_TDLS_CHAN_SWITCH_REQ, NO_SESSION,
                     chanSwitchParams->tdls_off_channel));
    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        pTdlsChanSwitchParams = vos_mem_malloc(sizeof(*pTdlsChanSwitchParams));
        if (NULL == pTdlsChanSwitchParams)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      FL("failed to allocate mem for tdls chan switch param"));
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }
        vos_mem_zero(pTdlsChanSwitchParams, sizeof(*pTdlsChanSwitchParams));

        switch (chanSwitchParams->tdls_off_ch_mode)
        {
            case ENABLE_CHANSWITCH:
                pTdlsChanSwitchParams->tdlsSwMode =
                                       WDA_TDLS_ENABLE_OFFCHANNEL;
                break;

            case DISABLE_CHANSWITCH:
                pTdlsChanSwitchParams->tdlsSwMode =
                                       WDA_TDLS_DISABLE_OFFCHANNEL;
                break;

            default:
                VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                          FL("invalid off channel command (%d)"),
                          chanSwitchParams->tdls_off_ch_mode);
                vos_mem_free(pTdlsChanSwitchParams);
                sme_ReleaseGlobalLock(&pMac->sme);
                return eHAL_STATUS_FAILURE;
        }

        vos_mem_copy(&pTdlsChanSwitchParams->peerMacAddr,
                     &chanSwitchParams->peer_mac_addr,
                     sizeof(tSirMacAddr));
        pTdlsChanSwitchParams->vdevId =
             chanSwitchParams->vdev_id;
        pTdlsChanSwitchParams->tdlsOffCh =
             chanSwitchParams->tdls_off_channel;
        pTdlsChanSwitchParams->tdlsOffChBwOffset =
             chanSwitchParams->tdls_off_ch_bw_offset;
        pTdlsChanSwitchParams->is_responder=
             chanSwitchParams->is_responder;
        pTdlsChanSwitchParams->operClass = chanSwitchParams->opclass;

        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                  FL("Country Code=%s, Requested offset=%d, Selected Operating Class=%d"),
                  pMac->scan.countryCodeCurrent,
                  pTdlsChanSwitchParams->tdlsOffChBwOffset,
                  pTdlsChanSwitchParams->operClass);

        vosMessage.type = WDA_TDLS_SET_OFFCHAN_MODE;
        vosMessage.reserved = 0;
        vosMessage.bodyptr = pTdlsChanSwitchParams;

        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus)) {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      FL("Message Post failed status=%d"), vosStatus);
            vos_mem_free(pTdlsChanSwitchParams);
            status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return(status);
}
#endif /* FEATURE_WLAN_TDLS */

eHalStatus sme_GetLinkSpeed(tHalHandle hHal, tSirLinkSpeedInfo *lsReq, void *plsContext,
                            void (*pCallbackfn)(tSirLinkSpeedInfo *indParam, void *pContext) )
{

    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS          vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);
    vos_msg_t           vosMessage;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        if ( (NULL == pCallbackfn) &&
            (NULL == pMac->sme.pLinkSpeedIndCb))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Indication Call back did not registered", __func__);
           sme_ReleaseGlobalLock(&pMac->sme);
           return eHAL_STATUS_FAILURE;
        }
        else if (NULL != pCallbackfn)
        {
           pMac->sme.pLinkSpeedCbContext = plsContext;
           pMac->sme.pLinkSpeedIndCb = pCallbackfn;
        }
        /* serialize the req through MC thread */
        vosMessage.bodyptr = lsReq;
        vosMessage.type    = WDA_GET_LINK_SPEED;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Post Link Speed msg fail", __func__);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return(status);
}


/**
 * sme_get_rssi() - get station's rssi
 * @hal: hal interface
 * @req: get rssi request information
 * @context: event handle context
 * @pcallbackfn: callback function pointer
 *
 * This function will send WDA_GET_RSSI to WMA
 *
 * Return: 0 on success, otherwise error value
 */
eHalStatus sme_get_rssi(tHalHandle hal, struct sir_rssi_req req,
			void *context,
			void (*callbackfn)(struct sir_rssi_resp *param,
						void *pcontext))
{

	eHalStatus          status    = eHAL_STATUS_SUCCESS;
	VOS_STATUS          vosstatus = VOS_STATUS_SUCCESS;
	tpAniSirGlobal      mac       = PMAC_STRUCT(hal);
	vos_msg_t           vosmessage;

	status = sme_AcquireGlobalLock(&mac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		if (NULL == callbackfn) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Indication Call back is NULL",
				__func__);
			sme_ReleaseGlobalLock(&mac->sme);
			return eHAL_STATUS_FAILURE;
		}

		mac->sme.pget_rssi_ind_cb = callbackfn;
		mac->sme.pget_rssi_cb_context = context;

		/* serialize the req through MC thread */
		vosmessage.bodyptr = vos_mem_malloc(sizeof(req));
		if (NULL == vosmessage.bodyptr) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Memory allocation failed.", __func__);
			sme_ReleaseGlobalLock(&mac->sme);
			return eHAL_STATUS_E_MALLOC_FAILED;
		}
		vos_mem_copy(vosmessage.bodyptr, &req, sizeof(req));
		vosmessage.type    = WDA_GET_RSSI;
		vosstatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosmessage);
		if (!VOS_IS_STATUS_SUCCESS(vosstatus)) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Post get rssi msg fail", __func__);
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&mac->sme);
	}
	return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_IsPmcBmps
    \API to Check if PMC state is BMPS.

    \- return v_BOOL_t
    -------------------------------------------------------------------------*/
v_BOOL_t sme_IsPmcBmps(tHalHandle hHal)
{
    return (BMPS == pmcGetPmcState(hHal));
}


eHalStatus sme_UpdateDfsSetting(tHalHandle hHal, tANI_U8 fUpdateEnableDFSChnlScan)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    smsLog(pMac, LOG2, FL("enter"));
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        pMac->scan.fEnableDFSChnlScan = fUpdateEnableDFSChnlScan;
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    smsLog(pMac, LOG2, FL("exit status %d"), status);

    return (status);
}

/*
 * SME API to enable/disable WLAN driver initiated SSR
 */
void sme_UpdateEnableSSR(tHalHandle hHal, tANI_BOOLEAN enableSSR)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus     status = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (HAL_STATUS_SUCCESS(status))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "SSR level is changed %d", enableSSR);
       /* not serializing this message, as this is only going
        * to set a variable in WDA/WDI
        */
        WDA_SetEnableSSR(enableSSR);
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return;
}

/*
 * SME API to stringify bonding mode. (hostapd convention)
 */

static const char* sme_CBMode2String( tANI_U32 mode)
{
   switch (mode)
   {
      case eCSR_INI_SINGLE_CHANNEL_CENTERED:
         return "HT20";
      case eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY:
         return "HT40-"; /* lower secondary channel */
      case eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY:
         return "HT40+"; /* upper secondary channel */
#ifdef WLAN_FEATURE_11AC
      case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW:
         return "VHT80+40+"; /* upper secondary channels */
      case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW:
         return "VHT80+40-"; /* 1 lower and 2 upper secondary channels */
      case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH:
         return "VHT80-40+"; /* 2 lower and 1 upper secondary channels */
      case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH:
         return "VHT80-40-"; /* lower secondary channels */
#endif
      default:
         VOS_ASSERT(0);
         return "Unknown";
   }
}

/*
 * SME API to adjust bonding mode to regulatory, dfs nol .. etc.
 *
 */
static VOS_STATUS sme_AdjustCBMode(tAniSirGlobal* pMac,
      tSmeConfigParams  *smeConfig,
      uint8_t channel, uint16_t *vht_channel_width)
{

   const tANI_U8 step = 4;
   tANI_U8 i, startChan = channel, chanCnt = 0, chanBitmap = 0;
   tANI_BOOLEAN violation = VOS_FALSE;
   tANI_U32 newMode, mode;
   tANI_U8 center_chan = channel;
   /* to validate 40MHz channels against the regulatory domain */
   tANI_BOOLEAN ht40_phymode = VOS_FALSE;

   /* get the bonding mode */
   mode = (channel <= 14) ? smeConfig->csrConfig.channelBondingMode24GHz :
                        smeConfig->csrConfig.channelBondingMode5GHz;
   newMode = mode;

   /* get the channels */
   switch (mode)
   {
      case eCSR_INI_SINGLE_CHANNEL_CENTERED:
         startChan = channel;
         chanCnt = 1;
         break;
      case eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY:
         startChan = channel - step;
         chanCnt = 2;
         center_chan = channel - CSR_CB_CENTER_CHANNEL_OFFSET;
         ht40_phymode = VOS_TRUE;
         break;
      case eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY:
         startChan = channel;
         chanCnt=2;
         center_chan = channel + CSR_CB_CENTER_CHANNEL_OFFSET;
         ht40_phymode = VOS_TRUE;
         break;
#ifdef WLAN_FEATURE_11AC
      case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW:
         startChan = channel;
         chanCnt = 4;
         break;
      case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW:
         startChan = channel - step;
         chanCnt = 4;
         break;
      case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH:
         startChan = channel - 2*step;
         chanCnt = 4;
         break;
      case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH:
         startChan = channel - 3*step;
         chanCnt = 4;
         break;
#endif
      default:
         VOS_ASSERT(0);
         return VOS_STATUS_E_FAILURE;
   }

   /* find violation; also map valid channels to a bitmap */
   for (i = 0; i < chanCnt; i++) {
      if (csrIsValidChannel(pMac, (startChan + (i * step))) ==
            VOS_STATUS_SUCCESS)
         chanBitmap = chanBitmap | 1 << i;
      else
         violation = VOS_TRUE;
   }

   /* validate if 40MHz channel is allowed */
   if (ht40_phymode) {
       if (!csrRoamIsValid40MhzChannel(pMac, center_chan))
          violation = VOS_TRUE;
   }

   /* no channels are valid */
   if (chanBitmap == 0)
   {
      /* never be in this case */
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
            FL("channel %d %s is not supported"),
            channel,
            sme_CBMode2String(mode));
      return VOS_STATUS_E_INVAL;
   }

   /* fix violation */
   if (violation)
   {
      const tANI_U8 lowerMask = 0x03, upperMask = 0x0c;
      /* fall back to single channel in all exception cases */
      newMode = eCSR_INI_SINGLE_CHANNEL_CENTERED;

      switch (mode)
      {
         case eCSR_INI_SINGLE_CHANNEL_CENTERED:
            /* fall thru */
         case eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY:
            /* fall thru */
         case eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY:
            break;
#ifdef WLAN_FEATURE_11AC
         case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW:
            if ((chanBitmap & lowerMask) == lowerMask)
               newMode = eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY;
            break;
         case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW:
            if ((chanBitmap & lowerMask) == lowerMask)
               newMode = eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY;
            break;
         case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH:
            if ((chanBitmap & upperMask) == upperMask)
               newMode = eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY;
            break;
         case eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH:
            if ((chanBitmap & upperMask) == upperMask)
               newMode = eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY;
            break;
#endif
         default:
            return VOS_STATUS_E_NOSUPPORT;
            break;
      }

      if (eCSR_INI_SINGLE_CHANNEL_CENTERED == newMode) {
          *vht_channel_width = eHT_CHANNEL_WIDTH_20MHZ;
      } else {
          *vht_channel_width = eHT_CHANNEL_WIDTH_40MHZ;
      }

      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_WARN,
            FL("bonding mode adjust: %s to %s"),
            sme_CBMode2String(mode),
            sme_CBMode2String(newMode));
   }

   /* check for mode change */
   if (newMode != mode)
   {
      if (channel <= 14)
          smeConfig->csrConfig.channelBondingMode24GHz = newMode;
      else
          smeConfig->csrConfig.channelBondingMode5GHz = newMode;
   }

   return VOS_STATUS_SUCCESS;

}

/*
 * SME API to determine the channel bonding mode
 */
eIniChanBondState sme_SelectCBMode(tHalHandle hHal, eCsrPhyMode eCsrPhyMode,
                            uint8_t channel, uint8_t ht_sec_ch,
                            uint16_t *vht_channel_width,
                            uint16_t ch_width_orig)
{
   tSmeConfigParams  smeConfig;
   tpAniSirGlobal    pMac = PMAC_STRUCT(hHal);
   eIniChanBondState cb_mode = eCSR_INI_SINGLE_CHANNEL_CENTERED;
   /* Donot check pMac->roam.configParam.channelBondingMode5GHz / 24GHz
    * Doing so results in circular reference
    */
   VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s: HW: %d CH: %d ORIG_BW: %d\n", __func__, eCsrPhyMode,
                channel, ch_width_orig);

   *vht_channel_width = ch_width_orig;
   vos_mem_zero(&smeConfig, sizeof (tSmeConfigParams));
   sme_GetConfigParam(pMac, &smeConfig);

   if ((eCSR_DOT11_MODE_11ac == eCsrPhyMode ||
        eCSR_DOT11_MODE_11ac_ONLY == eCsrPhyMode) &&
        (eHT_CHANNEL_WIDTH_80MHZ == ch_width_orig)) {
      if (channel== 36 || channel == 52 || channel == 100 ||
                channel == 116 || channel == 149 || channel == 132) {
          smeConfig.csrConfig.channelBondingMode5GHz =
                eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW;
      } else if (channel == 40 || channel == 56 || channel == 104 ||
                     channel == 120 || channel == 153 || channel == 136) {
          smeConfig.csrConfig.channelBondingMode5GHz =
                eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW;
      } else if (channel == 44 || channel == 60 || channel == 108 ||
                     channel == 124 || channel == 157 || channel == 140) {
          smeConfig.csrConfig.channelBondingMode5GHz =
                eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH;
      } else if (channel == 48 || channel == 64 || channel == 112 ||
                     channel == 128 || channel == 144 || channel == 161) {
          smeConfig.csrConfig.channelBondingMode5GHz =
                eCSR_INI_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH;
      } else if (channel == 165) {
          smeConfig.csrConfig.channelBondingMode5GHz =
                                     eCSR_INI_SINGLE_CHANNEL_CENTERED;
          *vht_channel_width = eHT_CHANNEL_WIDTH_20MHZ;
      } else if (channel >= 1 && channel < 5) {
          smeConfig.csrConfig.channelBondingMode24GHz =
                eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY;
          *vht_channel_width = eHT_CHANNEL_WIDTH_40MHZ;
      } else if (channel >= 5 && channel <= 9) {
          if (0 != ht_sec_ch) {
              if (ht_sec_ch > channel)
                  smeConfig.csrConfig.channelBondingMode24GHz =
                        eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY;
              else
                  smeConfig.csrConfig.channelBondingMode24GHz =
                        eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY;
	  }
          *vht_channel_width = eHT_CHANNEL_WIDTH_40MHZ;
      } else if (channel > 9 && channel <= 13) {
          smeConfig.csrConfig.channelBondingMode24GHz =
                eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY;
          *vht_channel_width = eHT_CHANNEL_WIDTH_40MHZ;
      } else if (channel ==14) {
          smeConfig.csrConfig.channelBondingMode24GHz =
                eCSR_INI_SINGLE_CHANNEL_CENTERED;
          *vht_channel_width = eHT_CHANNEL_WIDTH_20MHZ;
      }
   } else if ((eCSR_DOT11_MODE_11n == eCsrPhyMode ||
        eCSR_DOT11_MODE_11n_ONLY == eCsrPhyMode ||
        eCSR_DOT11_MODE_11ac == eCsrPhyMode ||
        eCSR_DOT11_MODE_11ac_ONLY == eCsrPhyMode) &&
        (eHT_CHANNEL_WIDTH_40MHZ == ch_width_orig)) {
       if (channel== 40 || channel == 48 || channel == 56 ||
                channel == 64 || channel == 104 || channel == 112 ||
                channel == 120 || channel == 128 || channel == 136 ||
                channel == 153 || channel == 161 || channel == 144) {
           smeConfig.csrConfig.channelBondingMode5GHz =
                                    eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY;
       } else if (channel== 36 || channel == 44 || channel == 52 ||
                channel == 60 || channel == 100 || channel == 108 ||
                channel == 116 || channel == 124 || channel == 132 ||
                channel == 149 || channel == 157 || channel == 140) {
           smeConfig.csrConfig.channelBondingMode5GHz =
                                        eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY;
       } else if (channel == 165) {
           smeConfig.csrConfig.channelBondingMode5GHz =
                                            eCSR_INI_SINGLE_CHANNEL_CENTERED;
           *vht_channel_width = eHT_CHANNEL_WIDTH_20MHZ;
       } else if (channel >= 1 && channel < 5) {
           smeConfig.csrConfig.channelBondingMode24GHz =
                                           eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY;
       } else if (channel >= 5 && channel <= 9) {
          if (ht_sec_ch > channel)
              smeConfig.csrConfig.channelBondingMode24GHz =
                    eCSR_INI_DOUBLE_CHANNEL_LOW_PRIMARY;
          else
              smeConfig.csrConfig.channelBondingMode24GHz =
                    eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY;
       } else if (channel > 9 && channel <= 13) {
           smeConfig.csrConfig.channelBondingMode24GHz =
                                           eCSR_INI_DOUBLE_CHANNEL_HIGH_PRIMARY;
       } else if (channel ==14) {
           smeConfig.csrConfig.channelBondingMode24GHz =
              eCSR_INI_SINGLE_CHANNEL_CENTERED;
           *vht_channel_width = eHT_CHANNEL_WIDTH_20MHZ;
       }
   } else {
       *vht_channel_width = eHT_CHANNEL_WIDTH_20MHZ;
       if (channel <= 14) {
           smeConfig.csrConfig.channelBondingMode24GHz =
                            eCSR_INI_SINGLE_CHANNEL_CENTERED;
       } else {
           smeConfig.csrConfig.channelBondingMode5GHz =
                            eCSR_INI_SINGLE_CHANNEL_CENTERED;
       }
   }

   sme_AdjustCBMode(pMac, &smeConfig, channel, vht_channel_width);
   sme_UpdateConfig (pMac, &smeConfig);
   cb_mode = (channel <= 14) ? smeConfig.csrConfig.channelBondingMode24GHz :
                        smeConfig.csrConfig.channelBondingMode5GHz;
   VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_WARN,
         "%s: CH: %d NEW_BW: %d %s-CB_Mode:%d", __func__, channel,
         *vht_channel_width, (channel <=14) ? "2G" : "5G", cb_mode);

   return cb_mode;
}

/*convert the ini value to the ENUM used in csr and MAC for CB state*/
ePhyChanBondState sme_GetCBPhyStateFromCBIniValue(tANI_U32 cb_ini_value)
{
   return(csrConvertCBIniValueToPhyCBState(cb_ini_value));
}
/*--------------------------------------------------------------------------

  \brief sme_SetCurrDeviceMode() - Sets the current operating device mode.
  \param hHal - The handle returned by macOpen.
  \param currDeviceMode - Current operating device mode.
  --------------------------------------------------------------------------*/

void sme_SetCurrDeviceMode (tHalHandle hHal, tVOS_CON_MODE currDeviceMode)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    pMac->sme.currDeviceMode = currDeviceMode;
    return;
}

/**
 * sme_set_pdev_ht_vht_ies() - sends the set pdev IE req
 *
 * @hal: Pointer to HAL
 * @enable2x2: 1x1 or 2x2 mode.
 *
 * Sends the set pdev IE req with Nss value.
 *
 * Return: None
 */
void sme_set_pdev_ht_vht_ies(tHalHandle hal, bool enable2x2)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	struct sir_set_ht_vht_cfg *ht_vht_cfg;
	eHalStatus status = eHAL_STATUS_FAILURE;

	if (!mac_ctx->per_band_chainmask_supp)
		return;

	if (!((mac_ctx->roam.configParam.uCfgDot11Mode ==
					eCSR_CFG_DOT11_MODE_AUTO) ||
				(mac_ctx->roam.configParam.uCfgDot11Mode ==
				 eCSR_CFG_DOT11_MODE_11N) ||
				(mac_ctx->roam.configParam.uCfgDot11Mode ==
				 eCSR_CFG_DOT11_MODE_11N_ONLY) ||
				(mac_ctx->roam.configParam.uCfgDot11Mode ==
				 eCSR_CFG_DOT11_MODE_11AC) ||
				(mac_ctx->roam.configParam.uCfgDot11Mode ==
				 eCSR_CFG_DOT11_MODE_11AC_ONLY)))
		return;

	status = sme_AcquireGlobalLock(&mac_ctx->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		ht_vht_cfg = vos_mem_malloc(sizeof(*ht_vht_cfg));
		if (NULL == ht_vht_cfg) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
					"%s: mem alloc failed for ht_vht_cfg",
					__func__);
			sme_ReleaseGlobalLock(&mac_ctx->sme);
			return;
		}

		ht_vht_cfg->pdev_id = 0;
		if (enable2x2)
			ht_vht_cfg->nss = 2;
		else
			ht_vht_cfg->nss = 1;
		ht_vht_cfg->dot11mode =
			(tANI_U8)csrTranslateToWNICfgDot11Mode(mac_ctx,
				mac_ctx->roam.configParam.uCfgDot11Mode);

		ht_vht_cfg->msg_type = eWNI_SME_PDEV_SET_HT_VHT_IE;
		ht_vht_cfg->len = sizeof(*ht_vht_cfg);
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
				"%s: send PDEV_SET_HT_VHT_IE with nss - %d, dot11mode - %d",
				__func__, ht_vht_cfg->nss, ht_vht_cfg->dot11mode);
		status = palSendMBMessage(mac_ctx->hHdd, ht_vht_cfg);
		if (eHAL_STATUS_SUCCESS != status) {
			smsLog(mac_ctx, LOGE, FL(
				"SME_PDEV_SET_HT_VHT_IE msg to PE failed"));
			vos_mem_free(ht_vht_cfg);
		}
		sme_ReleaseGlobalLock(&mac_ctx->sme);
	}
	return;
}
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
/*--------------------------------------------------------------------------
  \brief sme_HandoffRequest() - a wrapper function to Request a handoff
  from CSR.
  This is a synchronous call
  \param hHal - The handle returned by macOpen
  \param  sessionId - Session Identifier
  \param pHandoffInfo - info provided by HDD with the handoff request (namely:
  BSSID, channel etc.)
  \return eHAL_STATUS_SUCCESS - SME passed the request to CSR successfully.
          Other status means SME is failed to send the request.
  \sa
  --------------------------------------------------------------------------*/

eHalStatus sme_HandoffRequest(tHalHandle hHal,
                              tANI_U8 sessionId,
                              tCsrHandoffRequest *pHandoffInfo)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: invoked", __func__);
        status = csrHandoffRequest(pMac, sessionId, pHandoffInfo);
        sme_ReleaseGlobalLock( &pMac->sme );
    }

    return status ;
}

#ifdef IPA_UC_OFFLOAD
/* ---------------------------------------------------------------------------
    \fn sme_ipa_offload_enable_disable
    \brief  API to enable/disable IPA offload
    \param  hal - The handle returned by macOpen.
    \param  session_id - Session Identifier
    \param  request -  Pointer to the offload request.
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_ipa_offload_enable_disable(tHalHandle hal, tANI_U8 session_id,
                                struct sir_ipa_offload_enable_disable *request)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hal );
    eHalStatus status = eHAL_STATUS_FAILURE;
    struct sir_ipa_offload_enable_disable *request_buf;
    vos_msg_t msg;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status) {
        request_buf = vos_mem_malloc(sizeof(*request_buf));
        if (NULL == request_buf)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
               "%s: Not able to allocate memory for IPA_OFFLOAD_ENABLE_DISABLE",
               __func__);
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILED_ALLOC;
        }

        request_buf->offload_type = request->offload_type;
        request_buf->vdev_id = request->vdev_id;
        request_buf->enable = request->enable;

        msg.type     = WDA_IPA_OFFLOAD_ENABLE_DISABLE;
        msg.reserved = 0;
        msg.bodyptr  = request_buf;
        if (!VOS_IS_STATUS_SUCCESS(
            vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      "%s: Not able to post WDA_IPA_OFFLOAD_ENABLE_DISABLE message to WDA",
                      __func__);
            vos_mem_free(request_buf);
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }

        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return eHAL_STATUS_SUCCESS;
}
#endif

#endif

/*
 * SME API to check if there is any infra station or
 * P2P client is connected
 */
VOS_STATUS sme_isSta_p2p_clientConnected(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    if(csrIsInfraConnected(pMac))
    {
        return VOS_STATUS_SUCCESS;
    }
    return VOS_STATUS_E_FAILURE;
}


#ifdef FEATURE_WLAN_LPHB
/* ---------------------------------------------------------------------------
    \fn sme_LPHBConfigReq
    \API to make configuration LPHB within FW.
    \param hHal - The handle returned by macOpen
    \param lphdReq - LPHB request argument by client
    \param pCallbackfn - LPHB timeout notification callback function pointer
    \- return Configuration message posting status, SUCCESS or Fail
    -------------------------------------------------------------------------*/
eHalStatus sme_LPHBConfigReq
(
   tHalHandle hHal,
   tSirLPHBReq *lphdReq,
   void (*pCallbackfn)(void *pHddCtx, tSirLPHBInd *indParam)
)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS          vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);
    vos_msg_t           vosMessage;

    MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_RX_HDD_LPHB_CONFIG_REQ,
                                       NO_SESSION, lphdReq->cmd));
    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        if ((LPHB_SET_EN_PARAMS_INDID == lphdReq->cmd) &&
            (NULL == pCallbackfn) &&
            (NULL == pMac->sme.pLphbIndCb))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Indication Call back did not registered", __func__);
           sme_ReleaseGlobalLock(&pMac->sme);
           return eHAL_STATUS_FAILURE;
        }
        else if (NULL != pCallbackfn)
        {
           pMac->sme.pLphbIndCb = pCallbackfn;
        }

        /* serialize the req through MC thread */
        vosMessage.bodyptr = lphdReq;
        vosMessage.type    = WDA_LPHB_CONF_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Post Config LPHB MSG fail", __func__);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return(status);
}
#endif /* FEATURE_WLAN_LPHB */
/*--------------------------------------------------------------------------
  \brief sme_enable_disable_split_scan() - a wrapper function to set the split
                                          scan parameter.
  This is a synchronous call
  \param hHal - The handle returned by macOpen
  \return NONE.
  \sa
  --------------------------------------------------------------------------*/
void sme_enable_disable_split_scan (tHalHandle hHal, tANI_U8 nNumStaChan,
                                          tANI_U8 nNumP2PChan)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    pMac->roam.configParam.nNumStaChanCombinedConc = nNumStaChan;
    pMac->roam.configParam.nNumP2PChanCombinedConc = nNumP2PChan;

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                 "%s: SCAN nNumStaChanCombinedConc : %d,"
                           "nNumP2PChanCombinedConc : %d ",
                 __func__, nNumStaChan, nNumP2PChan);

    return;

}

/**
 * sme_AddPeriodicTxPtrn() - Add Periodic TX Pattern
 * @hal: global hal handle
 * @addPeriodicTxPtrnParams: request message
 *
 * Return: eHalStatus enumeration
 */
eHalStatus
sme_AddPeriodicTxPtrn(tHalHandle hal,
		      struct sSirAddPeriodicTxPtrn *addPeriodicTxPtrnParams)
{
	eHalStatus status     = eHAL_STATUS_SUCCESS;
	VOS_STATUS vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal mac   = PMAC_STRUCT(hal);
	struct sSirAddPeriodicTxPtrn *req_msg;
	vos_msg_t msg;

	smsLog(mac, LOG1, FL("enter"));

	req_msg = vos_mem_malloc(sizeof(*req_msg));
	if (!req_msg) {
		smsLog(mac, LOGE, FL("vos_mem_malloc failed"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	*req_msg = *addPeriodicTxPtrnParams;

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
			FL("sme_AcquireGlobalLock failed!(status=%d)"),
			status);
		vos_mem_free(req_msg);
		return status;
	}

	/* Serialize the req through MC thread */
	msg.bodyptr = req_msg;
	msg.type    = WDA_ADD_PERIODIC_TX_PTRN_IND;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                        NO_SESSION, msg.type));
	vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		smsLog(mac, LOGE,
			FL("vos_mq_post_message failed!(err=%d)"),
			vos_status);
		vos_mem_free(req_msg);
		status = eHAL_STATUS_FAILURE;
	}
	sme_ReleaseGlobalLock(&mac->sme);
	return status;
}

/**
 * sme_DelPeriodicTxPtrn() - Delete Periodic TX Pattern
 * @hal: global hal handle
 * @delPeriodicTxPtrnParams: request message
 *
 * Return: eHalStatus enumeration
 */
eHalStatus
sme_DelPeriodicTxPtrn(tHalHandle hal,
		      struct sSirDelPeriodicTxPtrn *delPeriodicTxPtrnParams)
{
	eHalStatus status     = eHAL_STATUS_SUCCESS;
	VOS_STATUS vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal mac   = PMAC_STRUCT(hal);
	struct sSirDelPeriodicTxPtrn *req_msg;
	vos_msg_t msg;

	smsLog(mac, LOG1, FL("enter"));

	req_msg = vos_mem_malloc(sizeof(*req_msg));
	if (!req_msg) {
		smsLog(mac, LOGE, FL("vos_mem_malloc failed"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	*req_msg = *delPeriodicTxPtrnParams;

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
			FL("sme_AcquireGlobalLock failed!(status=%d)"),
			status);
		vos_mem_free(req_msg);
		return status;
	}

	/* Serialize the req through MC thread */
	msg.bodyptr = req_msg;
	msg.type    = WDA_DEL_PERIODIC_TX_PTRN_IND;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                        NO_SESSION, msg.type));
	vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		smsLog(mac, LOGE,
			FL("vos_mq_post_message failed!(err=%d)"),
			vos_status);
		vos_mem_free(req_msg);
		status = eHAL_STATUS_FAILURE;
	}
	sme_ReleaseGlobalLock(&mac->sme);
	return status;
}

/**
 * sme_enable_rmc() - enable RMC
 * @hHal: handle
 * @sessionId: session id
 *
 * @Return: eHalStatus
 */
eHalStatus sme_enable_rmc(tHalHandle hHal, tANI_U32 sessionId)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    vos_msg_t vosMessage;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;

    smsLog(pMac, LOG1, FL("enable RMC"));
    status = sme_AcquireGlobalLock(&pMac->sme);
    if (HAL_STATUS_SUCCESS(status))
    {
        vosMessage.bodyptr = NULL;
        vosMessage.type = WDA_RMC_ENABLE_IND;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: failed to post message to WDA", __func__);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/**
 * sme_disable_rmc() - disable RMC
 * @hHal: handle
 * @sessionId: session id
 *
 * @Return: eHalStatus
 */
eHalStatus sme_disable_rmc(tHalHandle hHal, tANI_U32 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    vos_msg_t vosMessage;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;

   smsLog(pMac, LOG1, FL("disable RMC"));
   status = sme_AcquireGlobalLock(&pMac->sme);
   if (HAL_STATUS_SUCCESS(status))
   {
        vosMessage.bodyptr = NULL;
        vosMessage.type = WDA_RMC_DISABLE_IND;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: failed to post message to WDA", __func__);
           status = eHAL_STATUS_FAILURE;
        }
      sme_ReleaseGlobalLock(&pMac->sme);
   }
   return status;
}


/* ---------------------------------------------------------------------------
    \fn sme_SendRmcActionPeriod
    \brief  Used to send RMC action period param to fw
    \param  hHal
    \param  sessionId
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_SendRmcActionPeriod(tHalHandle hHal, tANI_U32 sessionId)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        vosMessage.bodyptr = NULL;
        vosMessage.type = WDA_RMC_ACTION_PERIOD_IND;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: failed to post message to WDA", __func__);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return(status);
}

/* ---------------------------------------------------------------------------
    \fn sme_GetIBSSPeerInfo
    \brief  Used to disable RMC
    setting will not persist over reboots
    \param  hHal
    \param  ibssPeerInfoReq  multicast Group IP address
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_RequestIBSSPeerInfo(tHalHandle hHal, void *pUserData,
                                            pIbssPeerInfoCb peerInfoCbk,
                                            tANI_BOOLEAN allPeerInfoReqd,
                                            tANI_U8 staIdx)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   VOS_STATUS vosStatus = VOS_STATUS_E_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   vos_msg_t vosMessage;
   tSirIbssGetPeerInfoReqParams *pIbssInfoReqParams;

   status = sme_AcquireGlobalLock(&pMac->sme);
   if ( eHAL_STATUS_SUCCESS == status)
   {
       pMac->sme.peerInfoParams.peerInfoCbk = peerInfoCbk;
       pMac->sme.peerInfoParams.pUserData = pUserData;

       pIbssInfoReqParams = (tSirIbssGetPeerInfoReqParams *)
                        vos_mem_malloc(sizeof(tSirIbssGetPeerInfoReqParams));
       if (NULL == pIbssInfoReqParams)
       {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: Not able to allocate memory for dhcp start", __func__);
           sme_ReleaseGlobalLock( &pMac->sme );
           return eHAL_STATUS_FAILURE;
       }
       pIbssInfoReqParams->allPeerInfoReqd = allPeerInfoReqd;
       pIbssInfoReqParams->staIdx = staIdx;

       vosMessage.type = WDA_GET_IBSS_PEER_INFO_REQ;
       vosMessage.bodyptr = pIbssInfoReqParams;
       vosMessage.reserved = 0;
       MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                NO_SESSION, vosMessage.type));
       vosStatus = vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
       if ( VOS_STATUS_SUCCESS != vosStatus )
       {
          VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                        "%s: Post WDA_GET_IBSS_PEER_INFO_REQ MSG failed", __func__);
          vos_mem_free(pIbssInfoReqParams);
          vosStatus = eHAL_STATUS_FAILURE;
       }
       sme_ReleaseGlobalLock( &pMac->sme );
   }

   return (vosStatus);
}

/* ---------------------------------------------------------------------------
    \fn sme_SendCesiumEnableInd
    \brief  Used to send proprietary cesium enable indication to fw
    \param  hHal
    \param  sessionId
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_SendCesiumEnableInd(tHalHandle hHal, tANI_U32 sessionId)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        vosMessage.bodyptr = NULL;
        vosMessage.type = WDA_IBSS_CESIUM_ENABLE_IND;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: failed to post message to WDA", __func__);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return(status);
}

void smeGetCommandQStatus( tHalHandle hHal )
{
    tSmeCmd *pTempCmd = NULL;
    tListElem *pEntry;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    if (NULL == pMac)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: pMac is NULL", __func__);
        return;
    }

    pEntry = csrLLPeekHead( &pMac->sme.smeCmdActiveList, LL_ACCESS_LOCK );
    if( pEntry )
    {
        pTempCmd = GET_BASE_ADDR( pEntry, tSmeCmd, Link );
    }
    smsLog( pMac, LOGE, "Currently smeCmdActiveList has command (0x%X)",
            (pTempCmd) ? pTempCmd->command : eSmeNoCommand );
    if(pTempCmd)
    {
        if( eSmeCsrCommandMask & pTempCmd->command )
        {
            //CSR command is stuck. See what the reason code is for that command
            dumpCsrCommandInfo(pMac, pTempCmd);
        }
    } //if(pTempCmd)

    smsLog( pMac, LOGE, "Currently smeCmdPendingList has %d commands",
            csrLLCount(&pMac->sme.smeCmdPendingList));

    smsLog( pMac, LOGE, "Currently roamCmdPendingList has %d commands",
            csrLLCount(&pMac->roam.roamCmdPendingList));

    return;
}

/* -------------------------------------------------------------------------
   \fn sme_set_dot11p_config
   \brief API to Set 802.11p config
   \param hal - The handle returned by macOpen
   \param enable_dot11p - 802.11p config param
   \return eHalStatus
---------------------------------------------------------------------------*/
void sme_set_dot11p_config(tHalHandle hal, bool enable_dot11p)
{
    tpAniSirGlobal mac = PMAC_STRUCT(hal);
    mac->enable_dot11p = enable_dot11p;
}

/**
 * copy_sir_ocb_config() - Performs deep copy of an OCB configuration
 * @src: the source configuration
 *
 * Return: pointer to the copied OCB configuration
 */
static struct sir_ocb_config *sme_copy_sir_ocb_config(struct sir_ocb_config *src)
{
	struct sir_ocb_config *dst;
	uint32_t length;
	void *cursor;

	length = sizeof(*src) +
		src->channel_count * sizeof(*src->channels) +
		src->schedule_size * sizeof(*src->schedule) +
		src->dcc_ndl_chan_list_len +
		src->dcc_ndl_active_state_list_len +
		src->def_tx_param_size;

	dst = vos_mem_malloc(length);
	if (!dst)
		return NULL;

	*dst = *src;

	cursor = dst;
	cursor += sizeof(*dst);
	dst->channels = cursor;
	cursor += src->channel_count * sizeof(*src->channels);
	vos_mem_copy(dst->channels, src->channels,
		     src->channel_count * sizeof(*src->channels));
	dst->schedule = cursor;
	cursor += src->schedule_size * sizeof(*src->schedule);
	vos_mem_copy(dst->schedule, src->schedule,
		     src->schedule_size * sizeof(*src->schedule));
	dst->dcc_ndl_chan_list = cursor;
	cursor += src->dcc_ndl_chan_list_len;
	vos_mem_copy(dst->dcc_ndl_chan_list, src->dcc_ndl_chan_list,
		     src->dcc_ndl_chan_list_len);
	dst->dcc_ndl_active_state_list = cursor;
	cursor += src->dcc_ndl_active_state_list_len;
	vos_mem_copy(dst->dcc_ndl_active_state_list,
		     src->dcc_ndl_active_state_list,
		     src->dcc_ndl_active_state_list_len);
	cursor += src->dcc_ndl_active_state_list_len;
	if (src->def_tx_param && src->def_tx_param_size) {
		dst->def_tx_param = cursor;
		vos_mem_copy(dst->def_tx_param, src->def_tx_param,
			     src->def_tx_param_size);
	}

	return dst;
}

/**
 * sme_ocb_set_config() - Set the OCB configuration
 * @hHal: reference to the HAL
 * @context: the context of the call
 * @callback: the callback to hdd
 * @config: the OCB configuration
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_ocb_set_config(tHalHandle hHal, void *context,
                              ocb_callback callback,
                              struct sir_ocb_config *config)
{
	eHalStatus status = eHAL_STATUS_FAILURE;
	VOS_STATUS vos_status = VOS_STATUS_E_FAILURE;
	tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
	vos_msg_t msg = {0};
	struct sir_ocb_config *msg_body;

	/* Lock the SME structure */
	status = sme_AcquireGlobalLock(&pMac->sme);
	if (!HAL_STATUS_SUCCESS(status))
		return status;

	/* Check if there is a pending request and return an error if one exists */
	if (pMac->sme.ocb_set_config_callback) {
		status = eHAL_STATUS_FW_PS_BUSY;
		goto end;
	}

	msg_body = sme_copy_sir_ocb_config(config);

	if (!msg_body) {
		status = eHAL_STATUS_FAILED_ALLOC;
		goto end;
	}

	msg.type = WDA_OCB_SET_CONFIG_CMD;
	msg.bodyptr = msg_body;

	/* Set the request callback and context */
	pMac->sme.ocb_set_config_callback = callback;
	pMac->sme.ocb_set_config_context = context;

	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA, &msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
		      FL("Error posting message to WDA: %d"), vos_status);
		pMac->sme.ocb_set_config_callback = callback;
		pMac->sme.ocb_set_config_context = context;
		vos_mem_free(msg_body);
		goto end;
	}

end:
	sme_ReleaseGlobalLock(&pMac->sme);

	if (status)
		return status;
	if (vos_status)
		return eHAL_STATUS_FAILURE;
	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_ocb_set_utc_time() - Set the OCB UTC time
 * @utc: the UTC time struct
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_ocb_set_utc_time(struct sir_ocb_utc *utc)
{
	vos_msg_t msg = {0};
	struct sir_ocb_utc *sme_utc;

	sme_utc = vos_mem_malloc(sizeof(*sme_utc));
	if (!sme_utc) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Malloc failed"));
		return eHAL_STATUS_E_MALLOC_FAILED;
	}
	*sme_utc = *utc;

	msg.type = WDA_OCB_SET_UTC_TIME_CMD;
	msg.reserved = 0;
	msg.bodyptr = sme_utc;
	if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA,
						       &msg))) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to post message to WDA"));
		vos_mem_free(utc);
		return eHAL_STATUS_FAILURE;
	}

	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_ocb_start_timing_advert() - Start sending timing advert frames
 * @timing_advert: the timing advertisement struct
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_ocb_start_timing_advert(
    struct sir_ocb_timing_advert *timing_advert)
{
	vos_msg_t msg;
	void *buf;
	struct sir_ocb_timing_advert *sme_timing_advert;

	buf = vos_mem_malloc(sizeof(*sme_timing_advert) +
			     timing_advert->template_length);
	if (!buf) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to allocate memory for start TA"));
		return eHAL_STATUS_E_MALLOC_FAILED;
	}

	sme_timing_advert = (struct sir_ocb_timing_advert *)buf;
	*sme_timing_advert = *timing_advert;
	sme_timing_advert->template_value = buf + sizeof(*sme_timing_advert);
	vos_mem_copy(sme_timing_advert->template_value,
	timing_advert->template_value, timing_advert->template_length);

	msg.type = WDA_OCB_START_TIMING_ADVERT_CMD;
	msg.reserved = 0;
	msg.bodyptr = buf;
	if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA,
						       &msg))) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to post msg to WDA"));
		return eHAL_STATUS_FAILURE;
	}

	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_ocb_stop_timing_advert() - Stop sending timing advert frames on a channel
 * @timing_advert: the timing advertisement struct
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_ocb_stop_timing_advert(
    struct sir_ocb_timing_advert *timing_advert)
{
	vos_msg_t msg;
	struct sir_ocb_timing_advert *sme_timing_advert;

	sme_timing_advert = vos_mem_malloc(sizeof(*timing_advert));
	if (!sme_timing_advert) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to allocate memory for stop TA"));
		return eHAL_STATUS_E_MALLOC_FAILED;
	}
	*sme_timing_advert = *timing_advert;

	msg.type = WDA_OCB_STOP_TIMING_ADVERT_CMD;
	msg.reserved = 0;
	msg.bodyptr = sme_timing_advert;
	if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA,
						       &msg))) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to post msg to WDA"));
		return eHAL_STATUS_FAILURE;
	}

	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_ocb_gen_timing_advert_frame() - generate TA frame and populate the buffer
 * @hHal: reference to the HAL
 * @self_addr: the self MAC address
 * @buf: the buffer that will contain the frame
 * @timestamp_offset: return for the offset of the timestamp field
 * @time_value_offset: return for the time_value field in the TA IE
 *
 * Return: the length of the buffer.
 */
int sme_ocb_gen_timing_advert_frame(tHalHandle hal_handle,
				    tSirMacAddr self_addr, uint8_t **buf,
				    uint32_t *timestamp_offset,
				    uint32_t *time_value_offset)
{
	int template_length;
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal_handle);

	template_length = schGenTimingAdvertFrame(mac_ctx, self_addr, buf,
						  timestamp_offset,
						  time_value_offset);
	return template_length;
}

/**
 * sme_ocb_get_tsf_timer() - Get the TSF timer value
 * @hHal: reference to the HAL
 * @context: the context of the call
 * @callback: the callback to hdd
 * @request: the TSF timer request
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_ocb_get_tsf_timer(tHalHandle hHal, void *context,
                                 ocb_callback callback,
                                 struct sir_ocb_get_tsf_timer *request)
{
	eHalStatus status = eHAL_STATUS_FAILURE;
	VOS_STATUS vos_status = VOS_STATUS_E_FAILURE;
	tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
	vos_msg_t msg = {0};
	struct sir_ocb_get_tsf_timer *msg_body;

	/* Lock the SME structure */
	status = sme_AcquireGlobalLock(&pMac->sme);
	if (!HAL_STATUS_SUCCESS(status))
		return status;

	/* Allocate memory for the WMI request, and copy the parameter */
	msg_body = vos_mem_malloc(sizeof(*msg_body));
	if (!msg_body) {
		status = eHAL_STATUS_FAILED_ALLOC;
		goto end;
	}
	*msg_body = *request;

	msg.type = WDA_OCB_GET_TSF_TIMER_CMD;
	msg.bodyptr = msg_body;

	/* Set the request callback and the context */
	pMac->sme.ocb_get_tsf_timer_callback = callback;
	pMac->sme.ocb_get_tsf_timer_context = context;

	/* Post the message to WDA */
	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA, &msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Error posting message to WDA: %d"), vos_status);
		pMac->sme.ocb_get_tsf_timer_callback = NULL;
		pMac->sme.ocb_get_tsf_timer_context = NULL;
		vos_mem_free(msg_body);
		goto end;
	}

end:
	sme_ReleaseGlobalLock(&pMac->sme);

	if (status)
		return status;
	if (vos_status)
		return eHAL_STATUS_FAILURE;
	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_dcc_get_stats() - Get the DCC stats
 * @hHal: reference to the HAL
 * @context: the context of the call
 * @callback: the callback to hdd
 * @request: the get DCC stats request
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_dcc_get_stats(tHalHandle hHal, void *context,
                             ocb_callback callback,
                             struct sir_dcc_get_stats *request)
{
	eHalStatus status = eHAL_STATUS_FAILURE;
	VOS_STATUS vos_status = VOS_STATUS_E_FAILURE;
	tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
	vos_msg_t msg = {0};
	struct sir_dcc_get_stats *msg_body;

	/* Lock the SME structure */
	status = sme_AcquireGlobalLock(&pMac->sme);
	if (!HAL_STATUS_SUCCESS(status))
		return status;

	/* Allocate memory for the WMI request, and copy the parameter */
	msg_body = vos_mem_malloc(sizeof(*msg_body) +
				  request->request_array_len);
	if (!msg_body) {
		status = eHAL_STATUS_FAILED_ALLOC;
		goto end;
	}
	*msg_body = *request;
	msg_body->request_array = (void *)msg_body + sizeof(*msg_body);
	vos_mem_copy(msg_body->request_array, request->request_array,
		     request->request_array_len);

	msg.type = WDA_DCC_GET_STATS_CMD;
	msg.bodyptr = msg_body;

	/* Set the request callback and context */
	pMac->sme.dcc_get_stats_callback = callback;
	pMac->sme.dcc_get_stats_context = context;

	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA, &msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Error posting message to WDA: %d"), vos_status);
		pMac->sme.dcc_get_stats_callback = callback;
		pMac->sme.dcc_get_stats_context = context;
		vos_mem_free(msg_body);
		goto end;
	}

end:
	sme_ReleaseGlobalLock(&pMac->sme);

	if (status)
		return status;
	if (vos_status)
		return eHAL_STATUS_FAILURE;
	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_dcc_clear_stats() - Clear the DCC stats
 * @vdev_id: vdev id for OCB interface
 * @dcc_stats_bitmap: the entries in the stats to clear
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_dcc_clear_stats(uint32_t vdev_id, uint32_t dcc_stats_bitmap)
{
	vos_msg_t msg = {0};
	struct sir_dcc_clear_stats *request =
		vos_mem_malloc(sizeof(struct sir_dcc_clear_stats));
	if (!request) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to allocate memory"));
		return eHAL_STATUS_E_MALLOC_FAILED;
	}
	vos_mem_zero(request, sizeof(*request));
	request->vdev_id = vdev_id;
	request->dcc_stats_bitmap = dcc_stats_bitmap;

	msg.type = WDA_DCC_CLEAR_STATS_CMD;
	msg.bodyptr = request;

	if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA,
						       &msg))) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to post msg to WDA"));
		vos_mem_free(request);
		return eHAL_STATUS_FAILURE;
	}

	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_dcc_update_ndl() - Update the DCC settings
 * @hHal: reference to the HAL
 * @context: the context of the call
 * @callback: the callback to hdd
 * @request: the update DCC request
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_dcc_update_ndl(tHalHandle hHal, void *context,
                              ocb_callback callback,
                              struct sir_dcc_update_ndl *request)
{
	eHalStatus status = eHAL_STATUS_FAILURE;
	VOS_STATUS vos_status = VOS_STATUS_E_FAILURE;
	tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
	vos_msg_t msg = {0};
	struct sir_dcc_update_ndl *msg_body;

	/* Lock the SME structure */
	status = sme_AcquireGlobalLock(&pMac->sme);
	if (!HAL_STATUS_SUCCESS(status))
		return status;

	/* Allocate memory for the WMI request, and copy the parameter */
	msg_body = vos_mem_malloc(sizeof(*msg_body) +
				  request->dcc_ndl_chan_list_len +
				  request->dcc_ndl_active_state_list_len);
	if (!msg_body) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Failed to allocate memory"));
		status = eHAL_STATUS_FAILED_ALLOC;
		goto end;
	}

	*msg_body = *request;

	msg_body->dcc_ndl_chan_list = (void *)msg_body + sizeof(*msg_body);
	msg_body->dcc_ndl_active_state_list = msg_body->dcc_ndl_chan_list +
		request->dcc_ndl_chan_list_len;
	vos_mem_copy(msg_body->dcc_ndl_chan_list, request->dcc_ndl_chan_list,
		     request->dcc_ndl_active_state_list_len);
	vos_mem_copy(msg_body->dcc_ndl_active_state_list,
		     request->dcc_ndl_active_state_list,
		     request->dcc_ndl_active_state_list_len);

	msg.type = WDA_DCC_UPDATE_NDL_CMD;
	msg.bodyptr = msg_body;

	/* Set the request callback and the context */
	pMac->sme.dcc_update_ndl_callback = callback;
	pMac->sme.dcc_update_ndl_context = context;

	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA, &msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Error posting message to WDA: %d"), vos_status);
		pMac->sme.dcc_update_ndl_callback = NULL;
		pMac->sme.dcc_update_ndl_context = NULL;
		vos_mem_free(msg_body);
		goto end;
	}

end:
	sme_ReleaseGlobalLock(&pMac->sme);

	if (status)
		return status;
	if (vos_status)
		return eHAL_STATUS_FAILURE;
	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_register_for_dcc_stats_event() - Register for the periodic DCC stats
 *                                      event
 * @hHal: reference to the HAL
 * @context: the context of the call
 * @callback: the callback to hdd
 *
 * Return: eHAL_STATUS_SUCCESS on success, eHAL_STATUS_FAILURE on failure
 */
eHalStatus sme_register_for_dcc_stats_event(tHalHandle hHal, void *context,
                                            ocb_callback callback)
{
	tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
	eHalStatus status = eHAL_STATUS_FAILURE;

	status = sme_AcquireGlobalLock(&pMac->sme);
	pMac->sme.dcc_stats_event_callback = callback;
	pMac->sme.dcc_stats_event_context = context;
	sme_ReleaseGlobalLock(&pMac->sme);

	return 0;
}

void sme_getRecoveryStats(tHalHandle hHal) {
    tANI_U8 i;

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "Self Recovery Stats");
    for (i = 0; i < MAX_ACTIVE_CMD_STATS; i++) {
        if (eSmeNoCommand != gSelfRecoveryStats.activeCmdStats[i].command) {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                        "timestamp %llu: command 0x%0X: reason %d: session %d",
                        gSelfRecoveryStats.activeCmdStats[i].timestamp,
                        gSelfRecoveryStats.activeCmdStats[i].command,
                        gSelfRecoveryStats.activeCmdStats[i].reason,
                        gSelfRecoveryStats.activeCmdStats[i].sessionId);
        }
    }
}

void sme_SaveActiveCmdStats(tHalHandle hHal) {
    tSmeCmd *pTempCmd = NULL;
    tListElem *pEntry;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    tANI_U8 statsIndx = 0;

    if (NULL == pMac) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: pMac is NULL", __func__);
        return;
    }

    pEntry = csrLLPeekHead(&pMac->sme.smeCmdActiveList, LL_ACCESS_LOCK);
    if (pEntry) {
        pTempCmd = GET_BASE_ADDR(pEntry, tSmeCmd, Link);
    }

    if (pTempCmd) {
        if (eSmeCsrCommandMask & pTempCmd->command) {
            statsIndx =  gSelfRecoveryStats.cmdStatsIndx;
            gSelfRecoveryStats.activeCmdStats[statsIndx].command =
                                                            pTempCmd->command;
            gSelfRecoveryStats.activeCmdStats[statsIndx].sessionId =
                                                          pTempCmd->sessionId;
            gSelfRecoveryStats.activeCmdStats[statsIndx].timestamp =
                                            vos_get_monotonic_boottime();
            if (eSmeCommandRoam == pTempCmd->command) {
                gSelfRecoveryStats.activeCmdStats[statsIndx].reason =
                                                pTempCmd->u.roamCmd.roamReason;
            } else if (eSmeCommandScan == pTempCmd->command) {
                gSelfRecoveryStats.activeCmdStats[statsIndx].reason =
                                                pTempCmd->u.scanCmd.reason;
            } else {
                gSelfRecoveryStats.activeCmdStats[statsIndx].reason = 0xFF;
            }

            gSelfRecoveryStats.cmdStatsIndx =
                   ((gSelfRecoveryStats.cmdStatsIndx + 1) &
                    (MAX_ACTIVE_CMD_STATS - 1));
        }
    }
    return;
}

void activeListCmdTimeoutHandle(void *userData)
{
    tHalHandle hal = (tHalHandle) userData;
    tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);

    if (NULL == mac_ctx) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_FATAL,
            "%s: pMac is null", __func__);
        return;
    }
    /* Return if no cmd pending in active list as
     * in this case we should not be here.
     */
    if (0 == csrLLCount(&mac_ctx->sme.smeCmdActiveList))
        return;

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
        "%s: Active List command timeout Cmd List Count %d", __func__,
        csrLLCount(&mac_ctx->sme.smeCmdActiveList) );
    smeGetCommandQStatus(hal);

    if (mac_ctx->roam.configParam.enable_fatal_event) {
        vos_flush_logs(WLAN_LOG_TYPE_FATAL,
                       WLAN_LOG_INDICATOR_HOST_DRIVER,
                       WLAN_LOG_REASON_SME_COMMAND_STUCK,
                       false);
    } else {
        vosTraceDumpAll(mac_ctx, 0, 0, 500, 0);
    }

    if (mac_ctx->sme.enableSelfRecovery) {
        sme_SaveActiveCmdStats(hal);
        vos_trigger_recovery();
    } else {
        if (!mac_ctx->roam.configParam.enable_fatal_event &&
            !(vos_is_load_unload_in_progress(VOS_MODULE_ID_SME, NULL) ||
            vos_is_logp_in_progress(VOS_MODULE_ID_SME, NULL)))
            vos_wlanRestart();
    }
}

VOS_STATUS sme_notify_modem_power_state(tHalHandle hHal, tANI_U32 value)
{
   vos_msg_t msg;
   tpSirModemPowerStateInd pRequestBuf;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   if (NULL == pMac)
   {
      return VOS_STATUS_E_FAILURE;
   }

   pRequestBuf = vos_mem_malloc(sizeof(tSirModemPowerStateInd));
   if (NULL == pRequestBuf)
   {
      VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
       "%s: Not able to allocate memory for MODEM POWER STATE IND",
       __func__);
      return VOS_STATUS_E_FAILURE;
   }

   pRequestBuf->param = value;

   msg.type     = WDA_MODEM_POWER_STATE_IND;
   msg.reserved = 0;
   msg.bodyptr  = pRequestBuf;
   if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)))
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
         "%s: Not able to post WDA_MODEM_POWER_STATE_IND message"
         " to WDA", __func__);
       vos_mem_free(pRequestBuf);
       return VOS_STATUS_E_FAILURE;
   }

   return VOS_STATUS_SUCCESS;
}

#ifdef QCA_HT_2040_COEX
VOS_STATUS sme_notify_ht2040_mode(tHalHandle hHal, tANI_U16 staId,
             v_MACADDR_t macAddrSTA, v_U8_t sessionId, tANI_U8 channel_type)
{
    vos_msg_t msg;
    tUpdateVHTOpMode *pHtOpMode = NULL;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

    if (NULL == pMac)
   {
      return VOS_STATUS_E_FAILURE;
   }

   pHtOpMode = vos_mem_malloc(sizeof(tUpdateVHTOpMode));
   if ( NULL == pHtOpMode )
   {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
       "%s: Not able to allocate memory for setting OP mode",
       __func__);
      return VOS_STATUS_E_FAILURE;
   }

   switch (channel_type)
   {
   case eHT_CHAN_HT20:
       pHtOpMode->opMode = eHT_CHANNEL_WIDTH_20MHZ;
       break;

   case eHT_CHAN_HT40MINUS:
   case eHT_CHAN_HT40PLUS:
       pHtOpMode->opMode = eHT_CHANNEL_WIDTH_40MHZ;
       break;

   default:
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
       "%s: Invalid OP mode",
       __func__);
       return VOS_STATUS_E_FAILURE;
   }

   pHtOpMode->staId = staId,
   vos_mem_copy(pHtOpMode->peer_mac, macAddrSTA.bytes,
                 sizeof(tSirMacAddr));
   pHtOpMode->smesessionId = sessionId;

   msg.type     = WDA_UPDATE_OP_MODE;
   msg.reserved = 0;
   msg.bodyptr  = pHtOpMode;
   if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)))
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
         "%s: Not able to post WDA_UPDATE_OP_MODE message"
         " to WDA", __func__);
       vos_mem_free(pHtOpMode);
       return VOS_STATUS_E_FAILURE;
   }

   VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
       "%s: Notifed FW about OP mode: %d for staId=%d",
       __func__, pHtOpMode->opMode, staId);


   return VOS_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------

    \fn sme_SetHT2040Mode

    \brief To update HT Operation beacon IE.

    \param hHal - The handle returned by macOPen
    \param sessionId - session id
    \param channel_type - indicates channel width
    \param obssEnabled - OBSS enabled/disabled

    \return eHalStatus  SUCCESS
                        FAILURE or RESOURCES
                        The API finished and failed.

  -------------------------------------------------------------------------------*/
eHalStatus sme_SetHT2040Mode(tHalHandle hHal, tANI_U8 sessionId,
                             tANI_U8 channel_type, tANI_BOOLEAN obssEnabled)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
   ePhyChanBondState cbMode;

   VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
       "%s: Update HT operation beacon IE, channel_type=%d",
       __func__, channel_type);

   switch (channel_type)
   {
   case eHT_CHAN_HT20:
       cbMode = PHY_SINGLE_CHANNEL_CENTERED;
       break;
   case eHT_CHAN_HT40MINUS:
       cbMode = PHY_DOUBLE_CHANNEL_HIGH_PRIMARY;
       break;
   case eHT_CHAN_HT40PLUS:
       cbMode = PHY_DOUBLE_CHANNEL_LOW_PRIMARY;
       break;
   default:
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
          "%s:Error!!! Invalid HT20/40 mode !",
          __func__);
       return VOS_STATUS_E_FAILURE;
   }
   status = sme_AcquireGlobalLock(&pMac->sme);
   if (HAL_STATUS_SUCCESS(status)) {
      status = csrSetHT2040Mode(pMac, sessionId, cbMode, obssEnabled);
      sme_ReleaseGlobalLock(&pMac->sme );
   }
   return (status);
}
#endif

/*
 * SME API to enable/disable idle mode power save
 * This should be called only if power save offload
 * is enabled
 */
VOS_STATUS sme_SetIdlePowersaveConfig(v_PVOID_t vosContext, tANI_U32 value)
{
    v_PVOID_t wdaContext = vos_get_context(VOS_MODULE_ID_WDA, vosContext);

    if (NULL == wdaContext)
    {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
              "%s: wdaContext is NULL", __func__);
       return VOS_STATUS_E_FAILURE;
    }
    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
              " Idle Ps Set Value %d", value);

    if (VOS_STATUS_SUCCESS != WDA_SetIdlePsConfig(wdaContext, value))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  " Failed to Set Idle Ps Value %d", value);
        return VOS_STATUS_E_FAILURE;
    }
    return VOS_STATUS_SUCCESS;
}
/**
 * sme_set_cts2self_for_p2p_go() - sme function to set ini parms to FW.
 * @hal_handle:                    reference to the HAL
 *
 * Return: hal_status
 */
eHalStatus sme_set_cts2self_for_p2p_go(tHalHandle hal_handle)
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	vos_msg_t vos_msg;

	vos_msg.bodyptr = NULL;
	vos_msg.type = WDA_SET_CTS2SELF_FOR_STA;
        if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA,
					&vos_msg))) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("Failed to post WDA_SET_CTS2SELF_FOR_STA to WDA"));
		status = eHAL_STATUS_FAILURE;
	}
	return status;
}

eHalStatus sme_ConfigEnablePowerSave (tHalHandle hHal, tPmcPowerSavingMode psMode)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status =  pmcOffloadConfigEnablePowerSave(hHal, psMode);
       sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}

eHalStatus sme_ConfigDisablePowerSave (tHalHandle hHal, tPmcPowerSavingMode psMode)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT( hHal );

   status = sme_AcquireGlobalLock( &pMac->sme );
   if ( HAL_STATUS_SUCCESS( status ) )
   {
       status =  pmcOffloadConfigDisablePowerSave(hHal, psMode);
       sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}

eHalStatus sme_PsOffloadEnablePowerSave (tHalHandle hHal, tANI_U32 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

   status = sme_AcquireGlobalLock(&pMac->sme);
   if(HAL_STATUS_SUCCESS( status ))
   {
       status =  PmcOffloadEnableStaModePowerSave(hHal, sessionId);
       sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}

eHalStatus sme_PsOffloadDisablePowerSave (tHalHandle hHal, tANI_U32 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

   status = sme_AcquireGlobalLock(&pMac->sme);
   if(HAL_STATUS_SUCCESS( status ))
   {
       status =  PmcOffloadDisableStaModePowerSave(hHal, sessionId);
       sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}

eHalStatus sme_PsOffloadEnableDeferredPowerSave (tHalHandle hHal,
                                                 tANI_U32 sessionId,
                                                 tANI_BOOLEAN isReassoc)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

   status = sme_AcquireGlobalLock(&pMac->sme);
   if (HAL_STATUS_SUCCESS( status ))
   {
       status =  PmcOffloadEnableDeferredStaModePowerSave(hHal, sessionId,
                                                          isReassoc);
       sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}

eHalStatus sme_PsOffloadDisableDeferredPowerSave (tHalHandle hHal,
                                                  tANI_U32 sessionId)
{
   eHalStatus status = eHAL_STATUS_FAILURE;
   tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

   status = sme_AcquireGlobalLock(&pMac->sme);
   if (HAL_STATUS_SUCCESS( status ))
   {
       status =  PmcOffloadDisableDeferredStaModePowerSave(hHal, sessionId);
       sme_ReleaseGlobalLock( &pMac->sme );
   }
   return (status);
}

tANI_S16 sme_GetHTConfig(tHalHandle hHal, tANI_U8 session_id, tANI_U16 ht_capab)
{
   tpAniSirGlobal    pMac = PMAC_STRUCT(hHal);
   tCsrRoamSession *pSession = CSR_GET_SESSION(pMac, session_id);

   if (NULL == pSession)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: pSession is NULL", __func__);
       return -EIO;
   }
   switch (ht_capab) {
   case WNI_CFG_HT_CAP_INFO_ADVANCE_CODING:
        return pSession->htConfig.ht_rx_ldpc;
   case WNI_CFG_HT_CAP_INFO_TX_STBC:
        return pSession->htConfig.ht_tx_stbc;
   case WNI_CFG_HT_CAP_INFO_RX_STBC:
        return pSession->htConfig.ht_rx_stbc;
   case WNI_CFG_HT_CAP_INFO_SHORT_GI_20MHZ:
   case WNI_CFG_HT_CAP_INFO_SHORT_GI_40MHZ:
        return pSession->htConfig.ht_sgi;
   default:
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "invalid ht capability");
        return -EIO;
   }
}

int sme_UpdateHTConfig(tHalHandle hHal, tANI_U8 sessionId, tANI_U16 htCapab,
                         int value)
{
   tpAniSirGlobal    pMac = PMAC_STRUCT(hHal);
   tCsrRoamSession *pSession = CSR_GET_SESSION(pMac, sessionId);

   if (NULL == pSession)
   {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: pSession is NULL", __func__);
       return -EIO;
   }

   if (eHAL_STATUS_SUCCESS != WDA_SetHTConfig(sessionId, htCapab, value)) {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                 "Failed to set ht capability in target");
       return -EIO;
   }

   switch (htCapab) {
   case WNI_CFG_HT_CAP_INFO_ADVANCE_CODING:
        pSession->htConfig.ht_rx_ldpc = value;
        break;
   case WNI_CFG_HT_CAP_INFO_TX_STBC:
        pSession->htConfig.ht_tx_stbc = value;
        break;
   case WNI_CFG_HT_CAP_INFO_RX_STBC:
        pSession->htConfig.ht_rx_stbc = value;
        break;
   case WNI_CFG_HT_CAP_INFO_SHORT_GI_20MHZ:
   case WNI_CFG_HT_CAP_INFO_SHORT_GI_40MHZ:
        pSession->htConfig.ht_sgi = value;
        break;
   }

   return 0;
}

#define HT20_SHORT_GI_MCS7_RATE 722
/* ---------------------------------------------------------------------------
    \fn sme_SendRateUpdateInd
    \brief  API to Update rate
    \param  hHal - The handle returned by macOpen
    \param  rateUpdateParams - Pointer to rate update params
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_SendRateUpdateInd(tHalHandle hHal,
                                 tSirRateUpdateInd *rateUpdateParams)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus status;
    vos_msg_t msg;
    tSirRateUpdateInd *rateUpdate = vos_mem_malloc(sizeof(tSirRateUpdateInd));
    if (rateUpdate == NULL) {
        VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                            "%s: SET_MC_RATE indication alloc fail", __func__);
        return eHAL_STATUS_FAILURE;
    }
    *rateUpdate = *rateUpdateParams;

    if (rateUpdate->mcastDataRate24GHz ==
            HT20_SHORT_GI_MCS7_RATE)
        rateUpdate->mcastDataRate24GHzTxFlag =
           eHAL_TX_RATE_HT20 | eHAL_TX_RATE_SGI;
    else if (rateUpdate->reliableMcastDataRate ==
             HT20_SHORT_GI_MCS7_RATE)
        rateUpdate->reliableMcastDataRateTxFlag =
           eHAL_TX_RATE_HT20 | eHAL_TX_RATE_SGI;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme)))
    {
        msg.type     = WDA_RATE_UPDATE_IND;
        msg.bodyptr  = rateUpdate;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                        NO_SESSION, msg.type));
        if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)))
        {
            VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,"%s: Not able "
                       "to post WDA_SET_RMC_RATE_IND to WDA!",
                       __func__);

            vos_mem_free(rateUpdate);
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }

        sme_ReleaseGlobalLock(&pMac->sme);
        return eHAL_STATUS_SUCCESS;
    }

    return status;
}

eHalStatus sme_getRegInfo(tHalHandle hHal, tANI_U8 chanId,
                         tANI_U32  *regInfo1, tANI_U32  *regInfo2)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus status;
    tANI_U8 i;
    eAniBoolean found = false;

    status = sme_AcquireGlobalLock(&pMac->sme);
    *regInfo1 = 0;
    *regInfo2 = 0;
    if (HAL_STATUS_SUCCESS(status))
    {
        for (i = 0 ; i < WNI_CFG_VALID_CHANNEL_LIST_LEN; i++)
        {
            if (pMac->scan.defaultPowerTable[i].chanId == chanId)
            {
                SME_SET_CHANNEL_REG_POWER(*regInfo1,
                                          pMac->scan.defaultPowerTable[i].pwr);

                SME_SET_CHANNEL_MAX_TX_POWER(*regInfo2,
                                          pMac->scan.defaultPowerTable[i].pwr);


                found = true;
                break;
            }
        }

        if (!found)
            status = eHAL_STATUS_FAILURE;

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

#ifdef FEATURE_WLAN_AUTO_SHUTDOWN
/* ---------------------------------------------------------------------------
    \fn sme_auto_shutdown_cb
    \brief  Used to plug in callback function for receiving auto shutdown evt
    \param  hHal
    \param  pCallbackfn : callback function pointer should be plugged in
    \- return eHalStatus
-------------------------------------------------------------------------*/
eHalStatus sme_set_auto_shutdown_cb
(
   tHalHandle hHal,
   void (*pCallbackfn)(void)
)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
              "%s: Plug in Auto shutdown event callback", __func__);

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        if (NULL != pCallbackfn)
        {
           pMac->sme.pAutoShutdownNotificationCb = pCallbackfn;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return(status);
}
/* ---------------------------------------------------------------------------
    \fn sme_set_auto_shutdown_timer
    \API to set auto shutdown timer value in FW.
    \param hHal - The handle returned by macOpen
    \param timer_val - The auto shutdown timer value to be set
    \- return Configuration message posting status, SUCCESS or Fail
    -------------------------------------------------------------------------*/
eHalStatus sme_set_auto_shutdown_timer(tHalHandle hHal, tANI_U32 timer_val)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS          vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);
    tSirAutoShutdownCmdParams *auto_sh_cmd;
    vos_msg_t           vosMessage;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        auto_sh_cmd = (tSirAutoShutdownCmdParams *)
                  vos_mem_malloc(sizeof(tSirAutoShutdownCmdParams));
        if (auto_sh_cmd == NULL)
        {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s Request Buffer Alloc Fail", __func__);
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }

        auto_sh_cmd->timer_val = timer_val;

        /* serialize the req through MC thread */
        vosMessage.bodyptr = auto_sh_cmd;
        vosMessage.type    = WDA_SET_AUTO_SHUTDOWN_TIMER_REQ;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Post Auto shutdown MSG fail", __func__);
           vos_mem_free(auto_sh_cmd);
           sme_ReleaseGlobalLock(&pMac->sme);
           return eHAL_STATUS_FAILURE;
        }
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: Posted Auto shutdown MSG", __func__);
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return(status);
}
#endif

#ifdef FEATURE_WLAN_CH_AVOID
/* ---------------------------------------------------------------------------
    \fn sme_AddChAvoidCallback
    \brief  Used to plug in callback function
            Which notify channel may not be used with SAP or P2PGO mode.
            Notification come from FW.
    \param  hHal
    \param  pCallbackfn : callback function pointer should be plugged in
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_AddChAvoidCallback
(
   tHalHandle hHal,
   void (*pCallbackfn)(void *pAdapter, void *indParam)
)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
              "%s: Plug in CH AVOID CB", __func__);

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        if (NULL != pCallbackfn)
        {
           pMac->sme.pChAvoidNotificationCb = pCallbackfn;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return(status);
}

/* ---------------------------------------------------------------------------
    \fn sme_ChAvoidUpdateReq
    \API to request channel avoidance update from FW.
    \param hHal - The handle returned by macOpen
    \param update_type - The udpate_type parameter of this request call
    \- return Configuration message posting status, SUCCESS or Fail
    -------------------------------------------------------------------------*/
eHalStatus sme_ChAvoidUpdateReq
(
   tHalHandle hHal
)
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS          vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);
    tSirChAvoidUpdateReq *cauReq;
    vos_msg_t           vosMessage;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        cauReq = (tSirChAvoidUpdateReq *)
                  vos_mem_malloc(sizeof(tSirChAvoidUpdateReq));
        if (NULL == cauReq)
        {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s Request Buffer Alloc Fail", __func__);
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }

        cauReq->reserved_param = 0;

        /* serialize the req through MC thread */
        vosMessage.bodyptr = cauReq;
        vosMessage.type    = WDA_CH_AVOID_UPDATE_REQ;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Post Ch Avoid Update MSG fail", __func__);
           vos_mem_free(cauReq);
           sme_ReleaseGlobalLock(&pMac->sme);
           return eHAL_STATUS_FAILURE;
        }
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     "%s: Posted Ch Avoid Update MSG", __func__);
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return(status);
}
#endif /* FEATURE_WLAN_CH_AVOID */

/**
 * sme_set_miracast() - Function to set miracast value to UMAC
 * @hal:                Handle returned by macOpen
 * @filter_type:        0-Disabled, 1-Source, 2-sink
 *
 * This function passes down the value of miracast set by
 * framework to UMAC
 *
 * Return: Configuration message posting status, SUCCESS or Fail
 *
 */
eHalStatus sme_set_miracast(tHalHandle hal, uint8_t filter_type)
{
	vos_msg_t msg;
	uint32_t *val;
	tpAniSirGlobal mac_ptr = PMAC_STRUCT(hal);

	val = vos_mem_malloc(sizeof(*val));
	if (NULL == val || NULL == mac_ptr) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Invalid pointer", __func__);
		return eHAL_STATUS_E_MALLOC_FAILED;
	}

	*val = filter_type;

	msg.type = SIR_HAL_SET_MIRACAST;
	msg.reserved = 0;
	msg.bodyptr = val;

	if (!VOS_IS_STATUS_SUCCESS(
				vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Not able to post WDA_SET_MAS_ENABLE_DISABLE to WMA!",
				__func__);
		vos_mem_free(val);
		return eHAL_STATUS_FAILURE;
	}

	mac_ptr->sme.miracast_value = filter_type;
	return eHAL_STATUS_SUCCESS;
}

/**
 * sme_set_mas() - Function to set MAS value to UMAC
 * @val:	1-Enable, 0-Disable
 *
 * This function passes down the value of MAS to the UMAC. A
 * value of 1 will enable MAS and a value of 0 will disable MAS
 *
 * Return: Configuration message posting status, SUCCESS or Fail
 *
 */
eHalStatus sme_set_mas(uint32_t val)
{
	vos_msg_t msg;
	uint32_t *ptr_val;

	ptr_val = vos_mem_malloc(sizeof(*ptr_val));
	if (NULL == ptr_val) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: could not allocate ptr_val", __func__);
		return eHAL_STATUS_E_MALLOC_FAILED;
	}

	*ptr_val = val;

	msg.type = SIR_HAL_SET_MAS;
	msg.reserved = 0;
	msg.bodyptr = ptr_val;

	if (!VOS_IS_STATUS_SUCCESS(
				vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Not able to post WDA_SET_MAS_ENABLE_DISABLE to WMA!",
				__func__);
		vos_mem_free(ptr_val);
		return eHAL_STATUS_FAILURE;
	}
	return eHAL_STATUS_SUCCESS;
}

/* -------------------------------------------------------------------------
   \fn sme_RoamChannelChangeReq
   \brief API to Indicate Channel change to new target channel
   \return eHalStatus
---------------------------------------------------------------------------*/
eHalStatus sme_RoamChannelChangeReq(tHalHandle hHal, tCsrBssid bssid,
                                    tANI_U32 cbMode, tCsrRoamProfile *pprofile)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    u_int8_t ch_mode;

    ch_mode = (pprofile->ChannelInfo.ChannelList[0] <= 14) ?
                      pMac->roam.configParam.channelBondingMode24GHz :
                      pMac->roam.configParam.channelBondingMode5GHz;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  FL("sapdfs: requested CBmode=%d & new negotiated CBmode=%d"),
                  cbMode, ch_mode);
        status = csrRoamChannelChangeReq(pMac, bssid, ch_mode, pprofile);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}

/* -------------------------------------------------------------------------
   \fn sme_ProcessChannelChangeResp
   \brief API to Indicate Channel change response message to SAP.
   \return eHalStatus
---------------------------------------------------------------------------*/
eHalStatus sme_ProcessChannelChangeResp(tpAniSirGlobal pMac,
                                     v_U16_t msg_type, void *pMsgBuf)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tCsrRoamInfo pRoamInfo = {0};
    eCsrRoamResult roamResult;
    tpSwitchChannelParams pChnlParams = (tpSwitchChannelParams) pMsgBuf;
    tANI_U32 SessionId = pChnlParams->peSessionId;

    pRoamInfo.channelChangeRespEvent =
    (tSirChanChangeResponse *)vos_mem_malloc(
                                sizeof(tSirChanChangeResponse));
    if (NULL == pRoamInfo.channelChangeRespEvent)
    {
        status = eHAL_STATUS_FAILURE;
        smsLog(pMac, LOGE, "Channel Change Event Allocation Failed: %d\n",
              status);
        return status;
    }
    if (msg_type == eWNI_SME_CHANNEL_CHANGE_RSP)
    {
        pRoamInfo.channelChangeRespEvent->sessionId = SessionId;
        pRoamInfo.channelChangeRespEvent->newChannelNumber =
                                           pChnlParams->channelNumber;
        pRoamInfo.channelChangeRespEvent->secondaryChannelOffset =
                                  pChnlParams->secondaryChannelOffset;

        if (pChnlParams->status == eHAL_STATUS_SUCCESS)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO_MED,
            "sapdfs: Received success eWNI_SME_CHANNEL_CHANGE_RSP for sessionId[%d]",
                      SessionId);
            pRoamInfo.channelChangeRespEvent->channelChangeStatus = 1;
            roamResult = eCSR_ROAM_RESULT_CHANNEL_CHANGE_SUCCESS;
        }
        else
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO_MED,
            "sapdfs: Received failure eWNI_SME_CHANNEL_CHANGE_RSP for sessionId[%d]",
                      SessionId);
            pRoamInfo.channelChangeRespEvent->channelChangeStatus = 0;
            roamResult = eCSR_ROAM_RESULT_CHANNEL_CHANGE_FAILURE;
        }

        csrRoamCallCallback(pMac, SessionId, &pRoamInfo, 0,
                                  eCSR_ROAM_SET_CHANNEL_RSP, roamResult);

    }
    else
    {
        status = eHAL_STATUS_FAILURE;
        smsLog(pMac, LOGE, "Invalid Channel Change Resp Message: %d\n",
              status);
    }
    vos_mem_free(pRoamInfo.channelChangeRespEvent);

    return status;
}

/* -------------------------------------------------------------------------
   \fn sme_RoamStartBeaconReq
   \brief API to Indicate LIM to start Beacon Tx
   \after SAP CAC Wait is completed.
   \param hHal - The handle returned by macOpen
   \param sessionId - session ID
   \param dfsCacWaitStatus - CAC WAIT status flag
   \return eHalStatus
---------------------------------------------------------------------------*/
eHalStatus sme_RoamStartBeaconReq( tHalHandle hHal, tCsrBssid bssid,
                              tANI_U8 dfsCacWaitStatus)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    status = sme_AcquireGlobalLock( &pMac->sme );

    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrRoamStartBeaconReq( pMac, bssid, dfsCacWaitStatus);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}

/* -------------------------------------------------------------------------
   \fn sme_RoamCsaIeRequest
   \brief API to request CSA IE transmission from PE
   \param hHal - The handle returned by macOpen
   \param pDfsCsaReq - CSA IE request
   \param bssid - SAP bssid
   \param ch_bandwidth - Channel offset
   \return eHalStatus
---------------------------------------------------------------------------*/
eHalStatus sme_RoamCsaIeRequest(tHalHandle hHal, tCsrBssid bssid,
                                    tANI_U8 targetChannel,
                                    tANI_U8 csaIeReqd,
                                    u_int8_t ch_bandwidth)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrRoamSendChanSwIERequest(pMac, bssid, targetChannel,
                                                  csaIeReqd, ch_bandwidth);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_InitThermalInfo
    \brief  SME API to initialize the thermal mitigation parameters
    \param  hHal
    \param  thermalParam : thermal mitigation parameters
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_InitThermalInfo( tHalHandle hHal,
                                tSmeThermalParams thermalParam )
{
    t_thermal_mgmt * pWdaParam;
    vos_msg_t msg;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pWdaParam = (t_thermal_mgmt *)vos_mem_malloc(sizeof(t_thermal_mgmt));
    if (NULL == pWdaParam)
    {
       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                 "%s: could not allocate tThermalMgmt", __func__);
       return eHAL_STATUS_E_MALLOC_FAILED;
    }

    vos_mem_zero((void*)pWdaParam, sizeof(t_thermal_mgmt));

    pWdaParam->thermalMgmtEnabled = thermalParam.smeThermalMgmtEnabled;
    pWdaParam->throttlePeriod = thermalParam.smeThrottlePeriod;
    pWdaParam->throttle_duty_cycle_tbl[0]=
        thermalParam.sme_throttle_duty_cycle_tbl[0];
    pWdaParam->throttle_duty_cycle_tbl[1]=
        thermalParam.sme_throttle_duty_cycle_tbl[1];
    pWdaParam->throttle_duty_cycle_tbl[2]=
        thermalParam.sme_throttle_duty_cycle_tbl[2];
    pWdaParam->throttle_duty_cycle_tbl[3]=
        thermalParam.sme_throttle_duty_cycle_tbl[3];
    pWdaParam->thermalLevels[0].minTempThreshold =
        thermalParam.smeThermalLevels[0].smeMinTempThreshold;
    pWdaParam->thermalLevels[0].maxTempThreshold =
        thermalParam.smeThermalLevels[0].smeMaxTempThreshold;
    pWdaParam->thermalLevels[1].minTempThreshold =
        thermalParam.smeThermalLevels[1].smeMinTempThreshold;
    pWdaParam->thermalLevels[1].maxTempThreshold =
        thermalParam.smeThermalLevels[1].smeMaxTempThreshold;
    pWdaParam->thermalLevels[2].minTempThreshold =
        thermalParam.smeThermalLevels[2].smeMinTempThreshold;
    pWdaParam->thermalLevels[2].maxTempThreshold =
        thermalParam.smeThermalLevels[2].smeMaxTempThreshold;
    pWdaParam->thermalLevels[3].minTempThreshold =
         thermalParam.smeThermalLevels[3].smeMinTempThreshold;
    pWdaParam->thermalLevels[3].maxTempThreshold =
         thermalParam.smeThermalLevels[3].smeMaxTempThreshold;

    if (eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock(&pMac->sme))
    {
        msg.type     = WDA_INIT_THERMAL_INFO_CMD;
        msg.bodyptr  = pWdaParam;

        if (!VOS_IS_STATUS_SUCCESS(
           vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)))
        {
            VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                       "%s: Not able to post WDA_SET_THERMAL_INFO_CMD to WDA!",
                       __func__);
            vos_mem_free(pWdaParam);
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
        return eHAL_STATUS_SUCCESS;
    }
    vos_mem_free(pWdaParam);
    return eHAL_STATUS_FAILURE;
}

/*
 * Plug in set thermal level callback
 */
void sme_add_set_thermal_level_callback(tHalHandle hHal,
                   tSmeSetThermalLevelCallback callback)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pMac->sme.set_thermal_level_cb = callback;
}

/**
 * sme_SetThermalLevel() - SME API to set the thermal mitigation level
 * hHal:	Handler to HAL
 * level:	Thermal mitigation level
 *
 * Return: HAL status code
 */
eHalStatus sme_SetThermalLevel( tHalHandle hHal, tANI_U8 level )
{
	vos_msg_t msg;
	tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
	VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;

	if (eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock(&pMac->sme)) {
		vos_mem_set(&msg, sizeof(msg), 0);
		msg.type = WDA_SET_THERMAL_LEVEL;
		msg.bodyval = level;

		vosStatus =  vos_mq_post_message(VOS_MODULE_ID_WDA, &msg);
		if (!VOS_IS_STATUS_SUCCESS(vosStatus)) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				   "%s: Not able to post WDA_SET_THERMAL_LEVEL to WDA!",
				   __func__);
			sme_ReleaseGlobalLock(&pMac->sme);
			return eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&pMac->sme);
		return eHAL_STATUS_SUCCESS;
	}
	return eHAL_STATUS_FAILURE;
}


/* ---------------------------------------------------------------------------
   \fn sme_TxpowerLimit
   \brief SME API to set txpower limits
   \param hHal
   \param psmetx : power limits for 2g/5g
   \- return eHalStatus
 -------------------------------------------------------------------------*/
eHalStatus sme_TxpowerLimit(tHalHandle hHal, tSirTxPowerLimit *psmetx)
{
     eHalStatus status = eHAL_STATUS_SUCCESS;
     VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
     vos_msg_t vosMessage;
     tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

     if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme)))
     {
          vosMessage.type = WDA_TX_POWER_LIMIT;
          vosMessage.reserved = 0;
          vosMessage.bodyptr = psmetx;

          vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
          if (!VOS_IS_STATUS_SUCCESS(vosStatus))
          {
             VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                        "%s: not able to post WDA_TX_POWER_LIMIT",
                        __func__);
             status = eHAL_STATUS_FAILURE;
             vos_mem_free(psmetx);
          }
          sme_ReleaseGlobalLock(&pMac->sme);
     }
     return(status);
}

eHalStatus sme_UpdateConnectDebug(tHalHandle hHal, tANI_U32 set_value)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    pMac->fEnableDebugLog = set_value;
    return (status);
}

/* ---------------------------------------------------------------------------
  \fn    sme_ApDisableIntraBssFwd

  \brief
    SME will send message to WMA to set Intra BSS in txrx

  \param

    hHal - The handle returned by macOpen

    sessionId - session id ( vdev id)

    disablefwd - boolean value that indicate disable intrabss fwd disable

  \return eHalStatus
--------------------------------------------------------------------------- */
eHalStatus sme_ApDisableIntraBssFwd(tHalHandle hHal, tANI_U8 sessionId,
                                    tANI_BOOLEAN disablefwd)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    int status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    vos_msg_t vosMessage;
    tpDisableIntraBssFwd pSapDisableIntraFwd = NULL;

    //Prepare the request to send to SME.
    pSapDisableIntraFwd = vos_mem_malloc(sizeof(tDisableIntraBssFwd));
    if (NULL == pSapDisableIntraFwd)
    {
       smsLog(pMac, LOGP, "Memory Allocation Failure!!! %s", __func__);
       return eSIR_MEM_ALLOC_FAILED;
    }

    vos_mem_zero(pSapDisableIntraFwd, sizeof(tDisableIntraBssFwd));

    pSapDisableIntraFwd->sessionId = sessionId;
    pSapDisableIntraFwd->disableintrabssfwd = disablefwd;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme)))
    {
        /* serialize the req through MC thread */
        vosMessage.bodyptr = pSapDisableIntraFwd;
        vosMessage.type    = WDA_SET_SAP_INTRABSS_DIS;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           status = eHAL_STATUS_FAILURE;
           vos_mem_free(pSapDisableIntraFwd);
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return (status);
}

#ifdef WLAN_FEATURE_STATS_EXT

/******************************************************************************
  \fn sme_StatsExtRegisterCallback

  \brief
  a function called to register the callback that send vendor event for stats
  ext

  \param callback - callback to be registered
******************************************************************************/
void sme_StatsExtRegisterCallback(tHalHandle hHal, StatsExtCallback callback)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pMac->sme.StatsExtCallback = callback;
}

/******************************************************************************
  \fn sme_StatsExtRequest

  \brief
  a function called when HDD receives STATS EXT vendor command from user space

  \param sessionID - vdevID for the stats ext request

  \param input - Stats Ext Request structure ptr

  \return eHalStatus
******************************************************************************/
eHalStatus sme_StatsExtRequest(tANI_U8 session_id, tpStatsExtRequestReq input)
{
    vos_msg_t msg;
    tpStatsExtRequest data;
    size_t data_len;

    data_len = sizeof(tStatsExtRequest) + input->request_data_len;
    data = vos_mem_malloc(data_len);

    if (data == NULL) {
        return eHAL_STATUS_FAILURE;
    }

    vos_mem_zero(data, data_len);
    data->vdev_id = session_id;
    data->request_data_len = input->request_data_len;
    if (input->request_data_len) {
        vos_mem_copy(data->request_data,
                     input->request_data, input->request_data_len);
    }

    msg.type = WDA_STATS_EXT_REQUEST;
    msg.reserved = 0;
    msg.bodyptr = data;

    if (VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA,
                                                  &msg)) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: Not able to post WDA_STATS_EXT_REQUEST message to WDA",
                  __func__);
        vos_mem_free(data);
        return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}


/******************************************************************************
  \fn sme_StatsExtEvent

  \brief
  a callback function called when SME received eWNI_SME_STATS_EXT_EVENT
  response from WDA

  \param hHal - HAL handle for device
  \param pMsg - Message body passed from WDA; includes NAN header
  \return eHalStatus
******************************************************************************/
eHalStatus sme_StatsExtEvent(tHalHandle hHal, void* pMsg)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus status = eHAL_STATUS_SUCCESS;

    if (NULL == pMsg) {
        smsLog(pMac, LOGE, "in %s msg ptr is NULL", __func__);
        status = eHAL_STATUS_FAILURE;
    } else {
        smsLog(pMac, LOG2, "SME: entering %s", __func__);

        if (pMac->sme.StatsExtCallback) {
            pMac->sme.StatsExtCallback(pMac->hHdd, (tpStatsExtEvent)pMsg);
        }
    }

    return status;
}

#endif

#if defined(CONFIG_HL_SUPPORT) && defined(QCA_BAD_PEER_TX_FLOW_CL)
/**
 * sme_init_bad_peer_txctl_info() - SME API to initialize the bad peer
 *                                  tx flow control parameters
 * @hHal:		handle of Hal.
 * @param:		Configuration of SME module.
 *
 * Read configuation from SME module setting, and then update the setting
 * to WMA module.
 *
 * Return: Status of the WMA message sending
 */
eHalStatus sme_init_bad_peer_txctl_info( tHalHandle hHal,
			struct sme_bad_peer_txctl_param param )
{
	struct t_bad_peer_txtcl_config *p_config;
	vos_msg_t msg;
	tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
	int i = 0;

	p_config = vos_mem_malloc(sizeof(struct t_bad_peer_txtcl_config));
	if (NULL == p_config) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: could not allocate tBadPeerInfo",
				__func__);
		return eHAL_STATUS_E_MALLOC_FAILED;
	}

	vos_mem_zero((void*)p_config, sizeof(struct t_bad_peer_txtcl_config));
	p_config->enable      =	param.enabled;
	p_config->period      = param.period;
	p_config->txq_limit   = param.txq_limit;
	p_config->tgt_backoff =	param.tgt_backoff;
	p_config->tgt_report_prd = param.tgt_report_prd;

	for (i = 0; i <= WLAN_WMA_IEEE80211_AC_LEVEL; i++) {
		p_config->threshold[i].cond       = param.thresh[i].cond;
		p_config->threshold[i].delta      = param.thresh[i].delta;
		p_config->threshold[i].percentage = param.thresh[i].percentage;
		p_config->threshold[i].thresh[0]  = param.thresh[i].thresh;
		p_config->threshold[i].txlimit    = param.thresh[i].limit;
	}

	if (eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock(&pMac->sme)) {
		msg.type     = WDA_INIT_BAD_PEER_TX_CTL_INFO_CMD;
		msg.bodyptr  = p_config;

		if (!VOS_IS_STATUS_SUCCESS (
		vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))) {
			VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Not able to post WDA_INIT_BAD_PEER_TX_CTL_INFO_CMD to WDA!",
				 __func__);

			vos_mem_free(p_config);
			sme_ReleaseGlobalLock(&pMac->sme);
			return eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&pMac->sme);
		return eHAL_STATUS_SUCCESS;
	}
	vos_mem_free(p_config);
	return eHAL_STATUS_FAILURE;
}
#endif /* defined(CONFIG_HL_SUPPORT) && defined(QCA_BAD_PEER_TX_FLOW_CL) */

/* ---------------------------------------------------------------------------
    \fn sme_UpdateDFSScanMode
    \brief  Update DFS roam scan mode
            This function is called through dynamic setConfig callback function
            to configure allowDFSChannelRoam.
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \param  allowDFSChannelRoam - DFS roaming scan mode 0 (disable),
            1 (passive), 2 (active)
    \return eHAL_STATUS_SUCCESS - SME update DFS roaming scan config
            successfully.
            Other status means SME failed to update DFS roaming scan config.
    \sa
    -------------------------------------------------------------------------*/
eHalStatus sme_UpdateDFSScanMode(tHalHandle hHal, tANI_U8 sessionId,
                                 v_U8_t allowDFSChannelRoam)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus          status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_DEBUG,
                     "LFR runtime successfully set AllowDFSChannelRoam Mode to "
                     "%d - old value is %d - roam state is %s",
                     allowDFSChannelRoam,
                     pMac->roam.configParam.allowDFSChannelRoam,
                     macTraceGetNeighbourRoamState(
                     pMac->roam.neighborRoamInfo[sessionId].neighborRoamState));
        pMac->roam.configParam.allowDFSChannelRoam = allowDFSChannelRoam;
        sme_ReleaseGlobalLock( &pMac->sme );
    }
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
       csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_UPDATE_CFG,
                          REASON_ROAM_DFS_SCAN_MODE_CHANGED);
    }
#endif

    return status ;
}

/*--------------------------------------------------------------------------
  \brief sme_GetDFSScanMode() - get DFS roam scan mode
            This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \return DFS roaming scan mode 0 (disable), 1 (passive), 2 (active)
  \sa
  --------------------------------------------------------------------------*/
v_U8_t sme_GetDFSScanMode(tHalHandle hHal)
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    return pMac->roam.configParam.allowDFSChannelRoam;
}


/*----------------------------------------------------------------------------
 \fn  sme_ModifyAddIE
 \brief  This function sends msg to updates the additional IE buffers in PE
 \param  hHal - global structure
 \param  pModifyIE - pointer to tModifyIE structure
 \param  updateType - type of buffer
 \- return Success or failure
-----------------------------------------------------------------------------*/
eHalStatus sme_ModifyAddIE(tHalHandle hHal,
                           tSirModifyIE *pModifyIE,
                           eUpdateIEsType updateType)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    status = sme_AcquireGlobalLock( &pMac->sme );

    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrRoamModifyAddIEs(pMac, pModifyIE, updateType);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}

/*----------------------------------------------------------------------------
 \fn  sme_UpdateAddIE
 \brief  This function sends msg to updates the additional IE buffers in PE
 \param  hHal - global structure
 \param  pUpdateIE - pointer to structure tUpdateIE
 \param  updateType - type of buffer
 \- return Success or failure
-----------------------------------------------------------------------------*/
eHalStatus sme_UpdateAddIE(tHalHandle hHal,
                           tSirUpdateIE *pUpdateIE,
                           eUpdateIEsType updateType)
{
    eHalStatus status = eHAL_STATUS_FAILURE;
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    status = sme_AcquireGlobalLock( &pMac->sme );

    if ( HAL_STATUS_SUCCESS( status ) )
    {
        status = csrRoamUpdateAddIEs(pMac, pUpdateIE, updateType);
        sme_ReleaseGlobalLock( &pMac->sme );
    }
    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_staInMiddleOfRoaming
    \brief  This function returns TRUE if STA is in the middle of roaming state
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \- return TRUE or FALSE
    -------------------------------------------------------------------------*/
tANI_BOOLEAN sme_staInMiddleOfRoaming(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac   = PMAC_STRUCT( hHal );
    eHalStatus     status = eHAL_STATUS_SUCCESS;
    tANI_BOOLEAN   ret    = FALSE;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        ret = csrNeighborMiddleOfRoaming(hHal, sessionId);
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return ret;
}

/* ---------------------------------------------------------------------------
    \fn sme_PsOffloadIsStaInPowerSave
    \brief  This function returns TRUE if STA is in power save
    \param  hHal - HAL handle for device
    \param  sessionId - Session Identifier
    \return TRUE or FALSE
    -------------------------------------------------------------------------*/
tANI_BOOLEAN sme_PsOffloadIsStaInPowerSave(tHalHandle hHal, tANI_U8 sessionId)
{
    tpAniSirGlobal pMac   = PMAC_STRUCT( hHal );
    eHalStatus     status = eHAL_STATUS_SUCCESS;
    tANI_BOOLEAN   ret    = FALSE;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        ret = pmcOffloadIsStaInPowerSave(pMac, sessionId);
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return ret;
}

VOS_STATUS sme_UpdateDSCPtoUPMapping( tHalHandle hHal,
                                      sme_QosWmmUpType  *dscpmapping,
                                      v_U8_t sessionId )
{
    tpAniSirGlobal pMac = PMAC_STRUCT( hHal );
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    v_U8_t i, j, peSessionId;
    tCsrRoamSession *pCsrSession = NULL;
    tpPESession pSession = NULL;

    status = sme_AcquireGlobalLock( &pMac->sme );
    if ( HAL_STATUS_SUCCESS( status ) )
    {
        pCsrSession = CSR_GET_SESSION( pMac, sessionId );
        if (pCsrSession == NULL)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Session lookup fails for CSR session", __func__);
            sme_ReleaseGlobalLock( &pMac->sme);
            return eHAL_STATUS_FAILURE;
        }
        if (!CSR_IS_SESSION_VALID( pMac, sessionId ))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Invalid session Id %u", __func__, sessionId);
            sme_ReleaseGlobalLock( &pMac->sme);
            return eHAL_STATUS_FAILURE;
        }

        pSession = peFindSessionByBssid( pMac,
            pCsrSession->connectedProfile.bssid, &peSessionId );

        if (pSession == NULL)
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                     "%s: Session lookup fails for BSSID", __func__);
            sme_ReleaseGlobalLock( &pMac->sme);
            return eHAL_STATUS_FAILURE;
        }

        if (!pSession->QosMapSet.present) {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                     FL("QOS Mapping IE not present"));
            sme_ReleaseGlobalLock( &pMac->sme);
            return eHAL_STATUS_FAILURE;
        }
        else
        {
            for (i = 0; i < SME_QOS_WMM_UP_MAX; i++)
            {
                for (j = pSession->QosMapSet.dscp_range[i][0];
                               j <= pSession->QosMapSet.dscp_range[i][1]; j++)
                {
                   if ((pSession->QosMapSet.dscp_range[i][0] == 255) &&
                                (pSession->QosMapSet.dscp_range[i][1] == 255))
                   {
                       dscpmapping[j]= 0;
                       VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                       "%s: User Priority %d is not used in mapping",
                                                             __func__, i);
                       break;
                   }
                   else
                   {
                       dscpmapping[j]= i;
                   }
                }
            }
            for (i = 0; i< pSession->QosMapSet.num_dscp_exceptions; i++)
            {
                if (pSession->QosMapSet.dscp_exceptions[i][0] != 255)
                {
                    dscpmapping[pSession->QosMapSet.dscp_exceptions[i][0] ] =
                                         pSession->QosMapSet.dscp_exceptions[i][1];
                }
            }
        }
    }
    sme_ReleaseGlobalLock( &pMac->sme);
    return status;
}

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
/* ---------------------------------------------------------------------------
    \fn sme_abortRoamScan
    \brief  API to abort current roam scan cycle by roam scan offload module.
    \param  hHal - The handle returned by macOpen.
    \param  sessionId - Session Identifier
    \return eHalStatus
  ---------------------------------------------------------------------------*/

eHalStatus sme_abortRoamScan(tHalHandle hHal, tANI_U8 sessionId)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    smsLog(pMac, LOGW, "entering function %s", __func__);
    if (pMac->roam.configParam.isRoamOffloadScanEnabled)
    {
        /* acquire the lock for the sme object */
        status = sme_AcquireGlobalLock(&pMac->sme);
        if (HAL_STATUS_SUCCESS(status))
        {
            csrRoamOffloadScan(pMac, sessionId, ROAM_SCAN_OFFLOAD_ABORT_SCAN,
                               REASON_ROAM_ABORT_ROAM_SCAN);
            /* release the lock for the sme object */
            sme_ReleaseGlobalLock( &pMac->sme );
        }
    }

    return(status);
}
#endif //#if WLAN_FEATURE_ROAM_SCAN_OFFLOAD

#ifdef FEATURE_WLAN_EXTSCAN
/* ---------------------------------------------------------------------------
    \fn sme_GetValidChannelsByBand
    \brief  SME API to fetch all valid channels filtered by band
    \param  hHal
    \param  wifiBand: RF band information
    \param  aValidChannels: output array to store channel info
    \param  pNumChannels: output number of channels
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_GetValidChannelsByBand(tHalHandle hHal,
                                      tANI_U8 wifiBand,
                                      tANI_U32 *aValidChannels,
                                      tANI_U8 *pNumChannels)
{
    eHalStatus status                                = eHAL_STATUS_SUCCESS;
    tANI_U8 chanList[WNI_CFG_VALID_CHANNEL_LIST_LEN] = {0};
    tpAniSirGlobal pMac                              = PMAC_STRUCT(hHal);
    tANI_U8 numChannels                              = 0;
    tANI_U8 i                                        = 0;
    tANI_U32 totValidChannels                 = WNI_CFG_VALID_CHANNEL_LIST_LEN;

    if (!aValidChannels || !pNumChannels) {
        smsLog(pMac, VOS_TRACE_LEVEL_ERROR,
               FL("Output channel list/NumChannels is NULL"));
        return eHAL_STATUS_INVALID_PARAMETER;
    }

    if ((wifiBand < WIFI_BAND_UNSPECIFIED) || (wifiBand >= WIFI_BAND_MAX)) {
        smsLog(pMac, VOS_TRACE_LEVEL_ERROR,
                     FL("Invalid wifiBand (%d)"), wifiBand);
        return eHAL_STATUS_INVALID_PARAMETER;
    }

    status = sme_GetCfgValidChannels(hHal, &chanList[0],
                                     &totValidChannels);
    if (!HAL_STATUS_SUCCESS(status)) {
        smsLog(pMac, VOS_TRACE_LEVEL_ERROR,
               FL("Failed to get valid channel list (err=%d)"), status);
        return status;
    }

    switch (wifiBand) {
    case WIFI_BAND_UNSPECIFIED:
            smsLog(pMac, VOS_TRACE_LEVEL_INFO, FL("Unspecified wifiBand, "
                         "return all (%d) valid channels"), totValidChannels);
            numChannels = totValidChannels;
            for (i = 0; i < totValidChannels; i++) {
                aValidChannels[i] = vos_chan_to_freq(chanList[i]);
            }
            break;

    case WIFI_BAND_BG:
            smsLog(pMac, VOS_TRACE_LEVEL_INFO, FL("WIFI_BAND_BG (2.4 GHz)"));
            for (i = 0; i < totValidChannels; i++) {
                if (CSR_IS_CHANNEL_24GHZ(chanList[i])) {
                    aValidChannels[numChannels++] =
                                                 vos_chan_to_freq(chanList[i]);
                }
            }
            break;

    case WIFI_BAND_A:
            smsLog(pMac, VOS_TRACE_LEVEL_INFO,
                          FL("WIFI_BAND_A (5 GHz without DFS)"));
            for (i = 0; i < totValidChannels; i++) {
                if (CSR_IS_CHANNEL_5GHZ(chanList[i]) &&
                             !CSR_IS_CHANNEL_DFS(chanList[i])) {
                    aValidChannels[numChannels++] =
                                                 vos_chan_to_freq(chanList[i]);
                }
            }
            break;

    case WIFI_BAND_ABG:
            smsLog(pMac, VOS_TRACE_LEVEL_INFO,
                          FL("WIFI_BAND_ABG (2.4 GHz + 5 GHz; no DFS)"));
            for (i = 0; i < totValidChannels; i++) {
                if ((CSR_IS_CHANNEL_24GHZ(chanList[i]) ||
                             CSR_IS_CHANNEL_5GHZ(chanList[i])) &&
                             !CSR_IS_CHANNEL_DFS(chanList[i])) {
                    aValidChannels[numChannels++] =
                                                 vos_chan_to_freq(chanList[i]);
                }
            }
            break;

    case WIFI_BAND_A_DFS_ONLY:
            smsLog(pMac, VOS_TRACE_LEVEL_INFO,
                          FL("WIFI_BAND_A_DFS (5 GHz DFS only)"));
            for (i = 0; i < totValidChannels; i++) {
                if (CSR_IS_CHANNEL_5GHZ(chanList[i]) &&
                             CSR_IS_CHANNEL_DFS(chanList[i])) {
                    aValidChannels[numChannels++] =
                                                 vos_chan_to_freq(chanList[i]);
                }
            }
            break;

    case WIFI_BAND_A_WITH_DFS:
            smsLog(pMac, VOS_TRACE_LEVEL_INFO,
                          FL("WIFI_BAND_A_WITH_DFS (5 GHz with DFS)"));
            for (i = 0; i < totValidChannels; i++) {
                if (CSR_IS_CHANNEL_5GHZ(chanList[i])) {
                    aValidChannels[numChannels++] =
                                                 vos_chan_to_freq(chanList[i]);
                }
            }
            break;

    case WIFI_BAND_ABG_WITH_DFS:
            smsLog(pMac, VOS_TRACE_LEVEL_INFO,
                     FL("WIFI_BAND_ABG_WITH_DFS (2.4 GHz + 5 GHz with DFS)"));
            for (i = 0; i < totValidChannels; i++) {
                if (CSR_IS_CHANNEL_24GHZ(chanList[i]) ||
                             CSR_IS_CHANNEL_5GHZ(chanList[i])) {
                    aValidChannels[numChannels++] =
                                                 vos_chan_to_freq(chanList[i]);
                }
            }
            break;

    default:
            smsLog(pMac, VOS_TRACE_LEVEL_ERROR,
                    FL("Unknown wifiBand (%d))"), wifiBand);
            return eHAL_STATUS_INVALID_PARAMETER;
            break;
    }
    *pNumChannels = numChannels;

    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_ExtScanGetCapabilities
    \brief  SME API to fetch extscan capabilities
    \param  hHal
    \param  pReq: extscan capabilities structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_ExtScanGetCapabilities (tHalHandle hHal,
                                     tSirGetExtScanCapabilitiesReqParams *pReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pReq;
        vosMessage.type    = WDA_EXTSCAN_GET_CAPABILITIES_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
           status = eHAL_STATUS_FAILURE;

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_ExtScanStart
    \brief  SME API to issue extscan start
    \param  hHal
    \param  pStartCmd: extscan start structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_ExtScanStart (tHalHandle hHal,
                           tSirWifiScanCmdReqParams *pStartCmd)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pStartCmd;
        vosMessage.type    = WDA_EXTSCAN_START_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
           status = eHAL_STATUS_FAILURE;

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_ExtScanStop
    \brief  SME API to issue extscan stop
    \param  hHal
    \param  pStopReq: extscan stop structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_ExtScanStop(tHalHandle hHal, tSirExtScanStopReqParams *pStopReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pStopReq;
        vosMessage.type    = WDA_EXTSCAN_STOP_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
           status = eHAL_STATUS_FAILURE;
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_SetBssHotlist
    \brief  SME API to set BSSID hotlist
    \param  hHal
    \param  pSetHotListReq: extscan set hotlist structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_SetBssHotlist (tHalHandle hHal,
                          tSirExtScanSetBssidHotListReqParams *pSetHotListReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pSetHotListReq;
        vosMessage.type    = WDA_EXTSCAN_SET_BSSID_HOTLIST_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
           status = eHAL_STATUS_FAILURE;

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_ResetBssHotlist
    \brief  SME API to reset BSSID hotlist
    \param  hHal
    \param  pSetHotListReq: extscan set hotlist structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_ResetBssHotlist (tHalHandle hHal,
                              tSirExtScanResetBssidHotlistReqParams *pResetReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pResetReq;
        vosMessage.type    = WDA_EXTSCAN_RESET_BSSID_HOTLIST_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
           status = eHAL_STATUS_FAILURE;

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/**
 * sme_set_ssid_hotlist() - Set the SSID hotlist
 * @hal: SME handle
 * @request: set ssid hotlist request
 *
 * Return: eHalStatus
 */
eHalStatus
sme_set_ssid_hotlist(tHalHandle hal,
		     struct sir_set_ssid_hotlist_request *request)
{
	eHalStatus status;
	VOS_STATUS vstatus;
	tpAniSirGlobal mac = PMAC_STRUCT(hal);
	vos_msg_t vos_message;
	struct sir_set_ssid_hotlist_request *set_req;

	set_req = vos_mem_malloc(sizeof(*set_req));
	if (!set_req) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  "%s: Not able to allocate memory for WDA_EXTSCAN_SET_SSID_HOTLIST_REQ",
			  __func__);
		return eHAL_STATUS_FAILURE;
	}

	*set_req = *request;
	status = sme_AcquireGlobalLock(&mac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		/* Serialize the req through MC thread */
		vos_message.bodyptr = set_req;
		vos_message.type    = WDA_EXTSCAN_SET_SSID_HOTLIST_REQ;
		vstatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_message);
		sme_ReleaseGlobalLock(&mac->sme);
		if (!VOS_IS_STATUS_SUCCESS(vstatus)) {
			vos_mem_free(set_req);
			status = eHAL_STATUS_FAILURE;
		}
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  "%s: sme_AcquireGlobalLock error", __func__);
		vos_mem_free(set_req);
		status = eHAL_STATUS_FAILURE;
	}
	return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_SetSignificantChange
    \brief  SME API to set significant change
    \param  hHal
    \param  pSetSignificantChangeReq: extscan set significant change structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_SetSignificantChange (tHalHandle hHal,
                    tSirExtScanSetSigChangeReqParams *pSetSignificantChangeReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pSetSignificantChangeReq;
        vosMessage.type    = WDA_EXTSCAN_SET_SIGNF_CHANGE_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
           status = eHAL_STATUS_FAILURE;

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_ResetSignificantChange
    \brief  SME API to reset significant change
    \param  hHal
    \param  pResetReq: extscan reset significant change structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_ResetSignificantChange (tHalHandle hHal,
                        tSirExtScanResetSignificantChangeReqParams *pResetReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pResetReq;
        vosMessage.type    = WDA_EXTSCAN_RESET_SIGNF_CHANGE_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
           status = eHAL_STATUS_FAILURE;

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_getCachedResults
    \brief  SME API to get cached results
    \param  hHal
    \param  pCachedResultsReq: extscan get cached results structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_getCachedResults (tHalHandle hHal,
                      tSirExtScanGetCachedResultsReqParams *pCachedResultsReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pCachedResultsReq;
        vosMessage.type    = WDA_EXTSCAN_GET_CACHED_RESULTS_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
           status = eHAL_STATUS_FAILURE;

        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

/**
 * sme_set_epno_list() - set epno network list
 * @hHal: global hal handle
 * @input: request message
 *
 * This function constructs the vos message and fill in message type,
 * bodyptr with %input and posts it to WDA queue.
 *
 * Return: eHalStatus enumeration
 */
eHalStatus sme_set_epno_list(tHalHandle hal,
				struct wifi_epno_params *input)
{
        eHalStatus status     = eHAL_STATUS_SUCCESS;
        VOS_STATUS vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal mac   = PMAC_STRUCT(hal);
	vos_msg_t vos_message;
	struct wifi_epno_params *req_msg;
	int len, i;

	smsLog(mac, LOG1, FL("enter"));
	len = sizeof(*req_msg) +
		    (input->num_networks * sizeof(struct wifi_epno_network));

	req_msg = vos_mem_malloc(len);
	if (!req_msg) {
		smsLog(mac, LOGE, FL("vos_mem_malloc failed"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	vos_mem_zero(req_msg, len);
	req_msg->num_networks = input->num_networks;
	req_msg->request_id = input->request_id;
	req_msg->session_id = input->session_id;

	/* Fill only when num_networks are non zero */
	if (req_msg->num_networks) {
		req_msg->min_5ghz_rssi = input->min_5ghz_rssi;
		req_msg->min_24ghz_rssi = input->min_24ghz_rssi;
		req_msg->initial_score_max = input->initial_score_max;
		req_msg->same_network_bonus = input->same_network_bonus;
		req_msg->secure_bonus = input->secure_bonus;
		req_msg->band_5ghz_bonus = input->band_5ghz_bonus;
		req_msg->current_connection_bonus =
			input->current_connection_bonus;

		for (i = 0; i < req_msg->num_networks; i++) {
			req_msg->networks[i].flags = input->networks[i].flags;
			req_msg->networks[i].auth_bit_field =
					input->networks[i].auth_bit_field;
			req_msg->networks[i].ssid.length =
					input->networks[i].ssid.length;
			vos_mem_copy(req_msg->networks[i].ssid.ssId,
					input->networks[i].ssid.ssId,
					req_msg->networks[i].ssid.length);
		}
	}

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
			FL("sme_AcquireGlobalLock failed!(status=%d)"),
			status);
		vos_mem_free(req_msg);
		return status;
	}

	/* Serialize the req through MC thread */
	vos_message.bodyptr = req_msg;
	vos_message.type    = WDA_SET_EPNO_LIST_REQ;
	vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		smsLog(mac, LOGE,
			FL("vos_mq_post_message failed!(err=%d)"),
			vos_status);
		vos_mem_free(req_msg);
		status = eHAL_STATUS_FAILURE;
	}
	sme_ReleaseGlobalLock(&mac->sme);
        return status;
}

/**
 * sme_set_passpoint_list() - set passpoint network list
 * @hal: global hal handle
 * @input: request message
 *
 * This function constructs the vos message and fill in message type,
 * bodyptr with @input and posts it to WDA queue.
 *
 * Return: eHalStatus enumeration
 */
eHalStatus sme_set_passpoint_list(tHalHandle hal,
				struct wifi_passpoint_req *input)
{
	eHalStatus status     = eHAL_STATUS_SUCCESS;
	VOS_STATUS vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal mac   = PMAC_STRUCT(hal);
	vos_msg_t vos_message;
	struct wifi_passpoint_req *req_msg;
	int len, i;

	smsLog(mac, LOG1, FL("enter"));
	len = sizeof(*req_msg) +
		(input->num_networks * sizeof(struct wifi_passpoint_network));
	req_msg = vos_mem_malloc(len);
	if (!req_msg) {
		smsLog(mac, LOGE, FL("vos_mem_malloc failed"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	vos_mem_zero(req_msg, len);
	req_msg->num_networks = input->num_networks;
	req_msg->request_id = input->request_id;
	req_msg->session_id = input->session_id;
	for (i = 0; i < req_msg->num_networks; i++) {
		req_msg->networks[i].id =
				input->networks[i].id;
		vos_mem_copy(req_msg->networks[i].realm,
				input->networks[i].realm,
				strlen(input->networks[i].realm) + 1);
		vos_mem_copy(req_msg->networks[i].plmn,
				input->networks[i].plmn,
				SIR_PASSPOINT_PLMN_LEN);
		vos_mem_copy(req_msg->networks[i].roaming_consortium_ids,
			     input->networks[i].roaming_consortium_ids,
			sizeof(req_msg->networks[i].roaming_consortium_ids));
	}

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
			FL("sme_AcquireGlobalLock failed!(status=%d)"),
			status);
		vos_mem_free(req_msg);
		return status;
	}

	/* Serialize the req through MC thread */
	vos_message.bodyptr = req_msg;
	vos_message.type    = WDA_SET_PASSPOINT_LIST_REQ;
	vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		smsLog(mac, LOGE,
			FL("vos_mq_post_message failed!(err=%d)"),
			vos_status);
		vos_mem_free(req_msg);
		status = eHAL_STATUS_FAILURE;
	}
	sme_ReleaseGlobalLock(&mac->sme);
        return status;
}

/**
 * sme_reset_passpoint_list() - reset passpoint network list
 * @hHal: global hal handle
 * @input: request message
 *
 * Return: eHalStatus enumeration
 */
eHalStatus sme_reset_passpoint_list(tHalHandle hal,
				    struct wifi_passpoint_req *input)
{
	eHalStatus status     = eHAL_STATUS_SUCCESS;
	VOS_STATUS vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal mac   = PMAC_STRUCT(hal);
	vos_msg_t vos_message;
	struct wifi_passpoint_req *req_msg;

	smsLog(mac, LOG1, FL("enter"));
	req_msg = vos_mem_malloc(sizeof(*req_msg));
	if (!req_msg) {
		smsLog(mac, LOGE, FL("vos_mem_malloc failed"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	vos_mem_zero(req_msg, sizeof(*req_msg));
	req_msg->request_id = input->request_id;
	req_msg->session_id = input->session_id;

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
			FL("sme_AcquireGlobalLock failed!(status=%d)"),
			status);
		vos_mem_free(req_msg);
		return status;
	}

	/* Serialize the req through MC thread */
	vos_message.bodyptr = req_msg;
	vos_message.type    = WDA_RESET_PASSPOINT_LIST_REQ;
	vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		smsLog(mac, LOGE,
			FL("vos_mq_post_message failed!(err=%d)"),
			vos_status);
		vos_mem_free(req_msg);
		status = eHAL_STATUS_FAILURE;
	}
	sme_ReleaseGlobalLock(&mac->sme);
        return status;
}

eHalStatus sme_ExtScanRegisterCallback (tHalHandle hHal,
                         void (*pExtScanIndCb)(void *, const tANI_U16, void *))
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        pMac->sme.pExtScanIndCb = pExtScanIndCb;
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

#endif /* FEATURE_WLAN_EXTSCAN */

/**
 * sme_set_rssi_threshold_breached_cb() - set rssi threshold breached callback
 * @hal: global hal handle
 * @cb: callback function pointer
 *
 * This function stores the rssi threshold breached callback function.
 *
 * Return: eHalStatus enumeration.
 */
eHalStatus sme_set_rssi_threshold_breached_cb(tHalHandle hal,
				void (*cb)(void *, struct rssi_breach_event *))
{
	eHalStatus status  = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal mac = PMAC_STRUCT(hal);

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
			FL("sme_AcquireGlobalLock failed!(status=%d)"),
			status);
		return status;
	}

	mac->sme.rssi_threshold_breached_cb = cb;
	sme_ReleaseGlobalLock(&mac->sme);
	return status;
}

/**
 * sme_set_rssi_monitoring() - set rssi monitoring
 * @hal: global hal handle
 * @input: request message
 *
 * This function constructs the vos message and fill in message type,
 * bodyptr with @input and posts it to WDA queue.
 *
 * Return: eHalStatus enumeration
 */
eHalStatus sme_set_rssi_monitoring(tHalHandle hal,
					struct rssi_monitor_req *input)
{
	eHalStatus status     = eHAL_STATUS_SUCCESS;
	VOS_STATUS vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal mac    = PMAC_STRUCT(hal);
	vos_msg_t vos_message;
	struct rssi_monitor_req *req_msg;

	smsLog(mac, LOG1, FL("enter"));
	req_msg = vos_mem_malloc(sizeof(*req_msg));
	if (!req_msg) {
		smsLog(mac, LOGE, FL("memory allocation failed"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	*req_msg = *input;

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
			FL("sme_AcquireGlobalLock failed!(status=%d)"),
			status);
		vos_mem_free(req_msg);
		return status;
	}

	/* Serialize the req through MC thread */
	vos_message.bodyptr = req_msg;
	vos_message.type    = WDA_SET_RSSI_MONITOR_REQ;
	vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		smsLog(mac, LOGE,
			FL("vos_mq_post_message failed!(err=%d)"),
			vos_status);
		vos_mem_free(req_msg);
	}
	sme_ReleaseGlobalLock(&mac->sme);

	return status;
}

#ifdef WLAN_FEATURE_LINK_LAYER_STATS

/* ---------------------------------------------------------------------------
    \fn sme_LLStatsClearReq
    \brief  SME API to clear Link Layer Statistics
    \param  hHal
    \param  pclearStatsReq: Link Layer clear stats request params structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_LLStatsClearReq (tHalHandle hHal,
                        tSirLLStatsClearReq *pclearStatsReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;
    tSirLLStatsClearReq *clear_stats_req;

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
              "staId = %u", pclearStatsReq->staId);
    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
              "statsClearReqMask = 0x%X",
              pclearStatsReq->statsClearReqMask);
    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
              "stopReq = %u", pclearStatsReq->stopReq);

    clear_stats_req = vos_mem_malloc(sizeof(*clear_stats_req));

    if (!clear_stats_req)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: Not able to allocate memory for WDA_LL_STATS_CLEAR_REQ",
                  __func__);
        return eHAL_STATUS_FAILURE;
    }

    *clear_stats_req = *pclearStatsReq;

    if (eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock(&pMac->sme))
    {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = clear_stats_req;
        vosMessage.type    = WDA_LINK_LAYER_STATS_CLEAR_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      "%s: not able to post WDA_LL_STATS_CLEAR_REQ",
                      __func__);
           vos_mem_free(clear_stats_req);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    else
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: "
                "sme_AcquireGlobalLock error", __func__);
        vos_mem_free(clear_stats_req);
        status = eHAL_STATUS_FAILURE;
    }

    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_LLStatsSetReq
    \brief  SME API to set the Link Layer Statistics
    \param  hHal
    \param  psetStatsReq: Link Layer set stats request params structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_LLStatsSetReq (tHalHandle hHal,
                        tSirLLStatsSetReq *psetStatsReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;
    tSirLLStatsSetReq *set_stats_req;

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
      "%s:  MPDU Size = %u", __func__,
        psetStatsReq->mpduSizeThreshold);
    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
      " Aggressive Stats Collections = %u",
      psetStatsReq->aggressiveStatisticsGathering);

    set_stats_req = vos_mem_malloc(sizeof(*set_stats_req));

    if (!set_stats_req)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: Not able to allocate memory for WDA_LL_STATS_SET_REQ",
                  __func__);
        return eHAL_STATUS_FAILURE;
    }

    *set_stats_req = *psetStatsReq;

    if (eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock(&pMac->sme))
    {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = set_stats_req;
        vosMessage.type    = WDA_LINK_LAYER_STATS_SET_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      "%s: not able to post WDA_LL_STATS_SET_REQ",
                      __func__);
           vos_mem_free(set_stats_req);
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    else
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: "
                "sme_AcquireGlobalLock error", __func__);
        vos_mem_free(set_stats_req);
        status = eHAL_STATUS_FAILURE;
    }

    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_LLStatsGetReq
    \brief  SME API to get the Link Layer Statistics
    \param  hHal
    \param  pgetStatsReq: Link Layer get stats request params structure
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_LLStatsGetReq (tHalHandle hHal,
                        tSirLLStatsGetReq *pgetStatsReq)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;
    tSirLLStatsGetReq *get_stats_req;

    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                  "reqId = %u", pgetStatsReq->reqId);
    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
              "staId = %u", pgetStatsReq->staId);
    VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
              "Stats Type = %u", pgetStatsReq->paramIdMask);

    get_stats_req = vos_mem_malloc(sizeof(*get_stats_req));

    if (!get_stats_req)
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: Not able to allocate memory for WDA_LL_STATS_GET_REQ",
                  __func__);
        return eHAL_STATUS_FAILURE;
    }

    *get_stats_req = *pgetStatsReq;

    if (eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock(&pMac->sme))
    {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = get_stats_req;
        vosMessage.type    = WDA_LINK_LAYER_STATS_GET_REQ;
        MTRACE(vos_trace(VOS_MODULE_ID_SME, TRACE_CODE_SME_TX_WDA_MSG,
                                 NO_SESSION, vosMessage.type));
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
           VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      "%s: not able to post WDA_LL_STATS_GET_REQ",
                      __func__);

           vos_mem_free(get_stats_req);
           status = eHAL_STATUS_FAILURE;

        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    else
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: "
                "sme_AcquireGlobalLock error", __func__);
        vos_mem_free(get_stats_req);
        status = eHAL_STATUS_FAILURE;
    }

    return status;
}

/* ---------------------------------------------------------------------------
    \fn sme_SetLinkLayerStatsIndCB
    \brief  SME API to trigger the stats are available  after get request
    \param  hHal
    \param callbackRoutine - HDD callback which needs to be invoked after
           getting status notification from FW
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_SetLinkLayerStatsIndCB
(
    tHalHandle hHal,
    void (*callbackRoutine) (void *callbackCtx, int indType, void *pRsp)
)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme)))
    {
        pMac->sme.pLinkLayerStatsIndCallback = callbackRoutine;
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    else
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR, "%s: "
                "sme_AcquireGlobalLock error", __func__);
    }

    return(status);
}

#endif /* WLAN_FEATURE_LINK_LAYER_STATS */

/**
 * sme_fw_mem_dump_register_cb() - Register fw memory dump callback
 *
 * @hHal - MAC global handle
 * @callback_routine - callback routine from HDD
 *
 * This API is invoked by HDD to register its callback in SME
 *
 * Return: eHalStatus
 */
#ifdef WLAN_FEATURE_MEMDUMP
eHalStatus sme_fw_mem_dump_register_cb(tHalHandle hal,
		void (*callback_routine)(void *cb_context,
					 struct fw_dump_rsp *rsp))
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal pmac = PMAC_STRUCT(hal);

	status = sme_AcquireGlobalLock(&pmac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		pmac->sme.fw_dump_callback = callback_routine;
		sme_ReleaseGlobalLock(&pmac->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("sme_AcquireGlobalLock error"));
	}

	return status;
}
#else
eHalStatus sme_fw_mem_dump_register_cb(tHalHandle hal,
		void (*callback_routine)(void *cb_context,
					 struct fw_dump_rsp *rsp))
{
	return eHAL_STATUS_SUCCESS;
}
#endif /* WLAN_FEATURE_MEMDUMP */

/**
 * sme_fw_mem_dump_unregister_cb() - Unregister fw memory dump callback
 *
 * @hHal - MAC global handle
 *
 * This API is invoked by HDD to unregister its callback in SME
 *
 * Return: eHalStatus
 */
#ifdef WLAN_FEATURE_MEMDUMP
eHalStatus sme_fw_mem_dump_unregister_cb(tHalHandle hal)
{
	eHalStatus status;
	tpAniSirGlobal pmac = PMAC_STRUCT(hal);

	status = sme_AcquireGlobalLock(&pmac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		pmac->sme.fw_dump_callback = NULL;
		sme_ReleaseGlobalLock(&pmac->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("sme_AcquireGlobalLock error"));
	}

	return status;
}
#else
eHalStatus sme_fw_mem_dump_unregister_cb(tHalHandle hal)
{
	return eHAL_STATUS_SUCCESS;
}
#endif /* WLAN_FEATURE_MEMDUMP */

#ifdef WLAN_FEATURE_ROAM_OFFLOAD
/*--------------------------------------------------------------------------
  \brief sme_UpdateRoamOffloadEnabled() - enable/disable roam offload feature
  It is used at in the REG_DYNAMIC_VARIABLE macro definition of
  \param hHal - The handle returned by macOpen.
  \param nRoamOffloadEnabled - The boolean to update with
  \return eHAL_STATUS_SUCCESS - SME update config successfully.
          Other status means SME is failed to update.
  \sa
  --------------------------------------------------------------------------*/

eHalStatus sme_UpdateRoamOffloadEnabled(tHalHandle hHal,
        v_BOOL_t nRoamOffloadEnabled)
{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (HAL_STATUS_SUCCESS(status))
    {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
        "%s: LFR3:gRoamOffloadEnabled is changed from %d to %d", __func__,
                              pMac->roam.configParam.isRoamOffloadEnabled,
                                                     nRoamOffloadEnabled);
        pMac->roam.configParam.isRoamOffloadEnabled = nRoamOffloadEnabled;
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return status ;
}

/*--------------------------------------------------------------------------
  \brief sme_UpdateRoamKeyMgmtOffloadEnabled() - enable/disable key mgmt offload
  This is a synchronous call
  \param hHal - The handle returned by macOpen.
  \param  sessionId - Session Identifier
  \param nRoamKeyMgmtOffloadEnabled - The boolean to update with
  \return eHAL_STATUS_SUCCESS - SME update config successfully.
          Other status means SME is failed to update.
  \sa
  --------------------------------------------------------------------------*/

eHalStatus sme_UpdateRoamKeyMgmtOffloadEnabled(tHalHandle hHal,
                                     tANI_U8 sessionId,
                                     v_BOOL_t nRoamKeyMgmtOffloadEnabled)

{
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    eHalStatus status    = eHAL_STATUS_SUCCESS;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (HAL_STATUS_SUCCESS(status))
    {
        if (CSR_IS_SESSION_VALID(pMac, sessionId)) {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
                      "%s: LFR3: KeyMgmtOffloadEnabled changed to %d",
                      __func__,
                      nRoamKeyMgmtOffloadEnabled);
            status = csrRoamSetKeyMgmtOffload(pMac,
                                              sessionId,
                                              nRoamKeyMgmtOffloadEnabled);
        } else {
            status = eHAL_STATUS_INVALID_PARAMETER;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return status ;
}
#endif

/* ---------------------------------------------------------------------------
   \fn sme_GetTemperature
   \brief SME API to get the pdev temperature
   \param hHal
   \param temperature context
   \param pCallbackfn: callback fn with response (temperature)
   \- return eHalStatus
   -------------------------------------------------------------------------*/
eHalStatus sme_GetTemperature(tHalHandle hHal,
        void *tempContext,
        void (*pCallbackfn)(int temperature, void *pContext))
{
    eHalStatus          status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS          vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal      pMac      = PMAC_STRUCT(hHal);
    vos_msg_t           vosMessage;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status)
    {
        if ( (NULL == pCallbackfn) &&
                (NULL == pMac->sme.pGetTemperatureCb))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                FL("Indication Call back did not registered"));
            sme_ReleaseGlobalLock(&pMac->sme);
            return eHAL_STATUS_FAILURE;
        }
        else if (NULL != pCallbackfn)
        {
            pMac->sme.pTemperatureCbContext = tempContext;
            pMac->sme.pGetTemperatureCb = pCallbackfn;
        }
        /* serialize the req through MC thread */
        vosMessage.bodyptr = NULL;
        vosMessage.type    = WDA_GET_TEMPERATURE_REQ;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                FL("Post Get Temperature msg fail"));
            status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return(status);
}

/* ---------------------------------------------------------------------------
    \fn sme_SetScanningMacOui
    \brief  SME API to set scanning mac oui
    \param  hHal
    \param  pScanMacOui: Scanning Mac Oui (input 3 bytes)
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_SetScanningMacOui(tHalHandle hHal, tSirScanMacOui *pScanMacOui)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = pScanMacOui;
        vosMessage.type    = WDA_SET_SCAN_MAC_OUI_REQ;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus)) {
           VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                FL("Msg post Set Scan Mac OUI failed"));
           status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}

#ifdef DHCP_SERVER_OFFLOAD
/* ---------------------------------------------------------------------------
    \fn sme_setDhcpSrvOffload
    \brief  SME API to set DHCP server offload info
    \param  hHal
    \param  pDhcpSrvInfo : DHCP server offload info struct
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_setDhcpSrvOffload(tHalHandle hHal,
                                tSirDhcpSrvOffloadInfo *pDhcpSrvInfo)
{
    vos_msg_t vosMessage;
    tSirDhcpSrvOffloadInfo *pSmeDhcpSrvInfo;
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pSmeDhcpSrvInfo = vos_mem_malloc(sizeof(*pSmeDhcpSrvInfo));

    if (!pSmeDhcpSrvInfo) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
         "%s: Not able to allocate memory for WDA_SET_DHCP_SERVER_OFFLOAD_CMD",
         __func__);
        return eHAL_STATUS_E_MALLOC_FAILED;
    }

    *pSmeDhcpSrvInfo = *pDhcpSrvInfo;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status) {
        /* serialize the req through MC thread */
        vosMessage.type     = WDA_SET_DHCP_SERVER_OFFLOAD_CMD;
        vosMessage.bodyptr  = pSmeDhcpSrvInfo;

        if (!VOS_IS_STATUS_SUCCESS(
            vos_mq_post_message(VOS_MODULE_ID_WDA, &vosMessage))) {
            VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s: Not able to post WDA_SET_DHCP_SERVER_OFFLOAD_CMD to WDA!",
                __func__);
            vos_mem_free(pSmeDhcpSrvInfo);
            status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    } else {
        VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                       "%s: sme_AcquireGlobalLock error!",
                       __func__);
        vos_mem_free(pSmeDhcpSrvInfo);
    }

    return (status);
}
#endif /* DHCP_SERVER_OFFLOAD */

#ifdef WLAN_FEATURE_GPIO_LED_FLASHING
/* ---------------------------------------------------------------------------
    \fn sme_SetLedFlashing
    \brief  API to set the Led flashing parameters.
    \param  hHal - The handle returned by macOpen.
    \param  x0, x1 -  led flashing parameters
    \param  gpio_num -  GPIO number
    \return eHalStatus
  ---------------------------------------------------------------------------*/
eHalStatus sme_SetLedFlashing (tHalHandle hHal, tANI_U8 type,
                               tANI_U32 x0, tANI_U32 x1, tANI_U32 gpio_num)
{
    eHalStatus status    = eHAL_STATUS_SUCCESS;
    VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
    vos_msg_t vosMessage;
    tSirLedFlashingReq *ledflashing;

    ledflashing = vos_mem_malloc(sizeof(*ledflashing));
    if (!ledflashing) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  FL("Not able to allocate memory for WDA_LED_TIMING_REQ"));
        return eHAL_STATUS_FAILURE;
    }

    ledflashing->pattern_id = type;
    ledflashing->led_x0 = x0;
    ledflashing->led_x1 = x1;
    ledflashing->gpio_num = gpio_num;

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* Serialize the req through MC thread */
        vosMessage.bodyptr = ledflashing;
        vosMessage.type    = WDA_LED_FLASHING_REQ;
        vosStatus = vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
        if (!VOS_IS_STATUS_SUCCESS(vosStatus))
            status = eHAL_STATUS_FAILURE;
        sme_ReleaseGlobalLock(&pMac->sme);
    }
    return status;
}
#endif

/* ---------------------------------------------------------------------------
    \fn sme_handle_dfS_chan_scan
    \brief  SME API to enable/disable DFS channel scan
    \param  hHal
    \param dfs_flag: whether dfs needs to be enabled or disabled
    \return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_handle_dfs_chan_scan(tHalHandle hHal, tANI_U8 dfs_flag)
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);

    status = sme_AcquireGlobalLock(&pMac->sme);

    if (eHAL_STATUS_SUCCESS == status) {

        pMac->scan.fEnableDFSChnlScan = dfs_flag;

        /* update the channel list to the firmware */
        status = csrUpdateChannelList(pMac);

        sme_ReleaseGlobalLock(&pMac->sme);
    }

    return status;
}

#ifdef MDNS_OFFLOAD
/* ---------------------------------------------------------------------------
    \fn sme_setMDNSOffload
    \brief  SME API to set mDNS offload info
    \param  hHal
    \param  pMDNSInfo : mDNS offload info struct
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_setMDNSOffload(tHalHandle hHal,
                                tSirMDNSOffloadInfo *pMDNSInfo)
{
    vos_msg_t vosMessage;
    tSirMDNSOffloadInfo *pSmeMDNSOffloadInfo;
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pSmeMDNSOffloadInfo = vos_mem_malloc(sizeof(*pSmeMDNSOffloadInfo));

    if (!pSmeMDNSOffloadInfo) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
         "%s: Not able to allocate memory for WDA_SET_MDNS_OFFLOAD_CMD",
         __func__);
        return eHAL_STATUS_E_MALLOC_FAILED;
    }

    *pSmeMDNSOffloadInfo = *pMDNSInfo;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status) {
        /* serialize the req through MC thread */
        vosMessage.type     = WDA_SET_MDNS_OFFLOAD_CMD;
        vosMessage.bodyptr  = pSmeMDNSOffloadInfo;

        if (!VOS_IS_STATUS_SUCCESS(
            vos_mq_post_message(VOS_MODULE_ID_WDA, &vosMessage))) {
            VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s: Not able to post WDA_SET_MDNS_OFFLOAD_CMD to WDA!",
                __func__);
            vos_mem_free(pSmeMDNSOffloadInfo);
            status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    } else {
        VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                       "%s: sme_AcquireGlobalLock error!",
                       __func__);
        vos_mem_free(pSmeMDNSOffloadInfo);
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_setMDNSFqdn
    \brief  SME API to set mDNS Fqdn info
    \param  hHal
    \param  pMDNSFqdnInfo : mDNS Fqdn info struct
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_setMDNSFqdn(tHalHandle hHal,
                                tSirMDNSFqdnInfo *pMDNSFqdnInfo)
{
    vos_msg_t vosMessage;
    tSirMDNSFqdnInfo *pSmeMDNSFqdnInfo;
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pSmeMDNSFqdnInfo = vos_mem_malloc(sizeof(*pSmeMDNSFqdnInfo));

    if (!pSmeMDNSFqdnInfo) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
         "%s: Not able to allocate memory for WDA_SET_MDNS_FQDN_CMD",
         __func__);
        return eHAL_STATUS_E_MALLOC_FAILED;
    }

    *pSmeMDNSFqdnInfo = *pMDNSFqdnInfo;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status) {
        /* serialize the req through MC thread */
        vosMessage.type     = WDA_SET_MDNS_FQDN_CMD;
        vosMessage.bodyptr  = pSmeMDNSFqdnInfo;

        if (!VOS_IS_STATUS_SUCCESS(
            vos_mq_post_message(VOS_MODULE_ID_WDA, &vosMessage))) {
            VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s: Not able to post WDA_SET_MDNS_FQDN_CMD to WDA!",
                __func__);
            vos_mem_free(pSmeMDNSFqdnInfo);
            status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    } else {
        VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                       "%s: sme_AcquireGlobalLock error!",
                       __func__);
        vos_mem_free(pSmeMDNSFqdnInfo);
    }

    return (status);
}

/* ---------------------------------------------------------------------------
    \fn sme_setMDNSResponse
    \brief  SME API to set mDNS response info
    \param  hHal
    \param  pMDNSRespInfo : mDNS response info struct
    \- return eHalStatus
    -------------------------------------------------------------------------*/
eHalStatus sme_setMDNSResponse(tHalHandle hHal,
                                tSirMDNSResponseInfo *pMDNSRespInfo)
{
    vos_msg_t vosMessage;
    tSirMDNSResponseInfo *pSmeMDNSRespInfo;
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pSmeMDNSRespInfo = vos_mem_malloc(sizeof(*pSmeMDNSRespInfo));

    if (!pSmeMDNSRespInfo) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
         "%s: Not able to allocate memory for WDA_SET_MDNS_RESPONSE_CMD",
         __func__);
        return eHAL_STATUS_E_MALLOC_FAILED;
    }

    *pSmeMDNSRespInfo = *pMDNSRespInfo;

    status = sme_AcquireGlobalLock(&pMac->sme);
    if (eHAL_STATUS_SUCCESS == status) {
        /* serialize the req through MC thread */
        vosMessage.type     = WDA_SET_MDNS_RESPONSE_CMD;
        vosMessage.bodyptr  = pSmeMDNSRespInfo;

        if (!VOS_IS_STATUS_SUCCESS(
            vos_mq_post_message(VOS_MODULE_ID_WDA, &vosMessage))) {
            VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                "%s: Not able to post WDA_SET_MDNS_RESPONSE_CMD to WDA!",
                __func__);
            vos_mem_free(pSmeMDNSRespInfo);
            status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    } else {
        VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                       "%s: sme_AcquireGlobalLock error!",
                       __func__);
        vos_mem_free(pSmeMDNSRespInfo);
    }

    return (status);
}
#endif /* MDNS_OFFLOAD */

#ifdef SAP_AUTH_OFFLOAD
/**
 * sme_set_sap_auth_offload() enable/disable Software AP Auth Offload
 * @hHal: hal layer handler
 * @sap_auth_offload_info: the information of Software AP Auth offload
 *
 * This function provide enable/disable Software AP authenticaiton offload
 * feature on target firmware
 *
 * Return: Return eHalStatus.
 */
eHalStatus sme_set_sap_auth_offload(tHalHandle hHal,
                      struct tSirSapOffloadInfo *sap_auth_offload_info)
{
    vos_msg_t vosMessage;
    struct tSirSapOffloadInfo *sme_sap_auth_offload_info;
    eHalStatus status = eHAL_STATUS_SUCCESS;
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

    pMac->sap_auth_offload_sec_type =
        sap_auth_offload_info->sap_auth_offload_sec_type;
    pMac->sap_auth_offload = sap_auth_offload_info->sap_auth_offload_enable;

    sme_sap_auth_offload_info =
        vos_mem_malloc(sizeof(*sme_sap_auth_offload_info));

    if (!sme_sap_auth_offload_info) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: Not able to allocate memory for WDA_SET_SAP_AUTH_OFL",
                  __func__);
        return eHAL_STATUS_E_MALLOC_FAILED;
    }

    vos_mem_copy(sme_sap_auth_offload_info, sap_auth_offload_info,
            sizeof(*sap_auth_offload_info));

    if (eHAL_STATUS_SUCCESS == (status = sme_AcquireGlobalLock(&pMac->sme))) {
        /* serialize the req through MC thread */
        vosMessage.type     = WDA_SET_SAP_AUTH_OFL;
        vosMessage.bodyptr  = sme_sap_auth_offload_info;

        if (!VOS_IS_STATUS_SUCCESS(
            vos_mq_post_message(VOS_MODULE_ID_WDA, &vosMessage))) {
            VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                      "%s: Not able to post WDA_SET_SAP_AUTH_OFL to WDA!",
                      __func__);
            vos_mem_free(sme_sap_auth_offload_info);
            status = eHAL_STATUS_FAILURE;
        }
        sme_ReleaseGlobalLock(&pMac->sme);
    } else {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                  "%s: sme_AcquireGlobalLock error!",
                  __func__);
        vos_mem_free(sme_sap_auth_offload_info);
        status = eHAL_STATUS_FAILURE;
    }

    return (status);

}

/**
 * sme_set_client_block_info set client block configuration.
 *
 * @hHal: hal layer handler
 * @client_block_info: client block info struct pointer
 *
 * This function set client block info including reconnect_cnt,
 * con_fail_duration, block_duration.
 *
 * Return: Return eHalStatus.
 */
eHalStatus sme_set_client_block_info(tHalHandle hHal,
		struct sblock_info *pclient_block_info)
{
	vos_msg_t vosMessage;
	eHalStatus status;
	tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
	VOS_STATUS vos_status;
	struct sblock_info *client_block_info;

	if (!pclient_block_info) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			"%s: invalid sblock_info pointer", __func__);
		return eHAL_STATUS_INVALID_PARAMETER;
	}

	client_block_info = vos_mem_malloc(sizeof(*client_block_info));
	if (NULL == client_block_info) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			"%s: fail to alloc sblock_info",
			__func__);
		return eHAL_STATUS_FAILURE;
	}

	*client_block_info = *pclient_block_info;

	status = sme_AcquireGlobalLock(&pMac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		vosMessage.type     = WDA_SET_CLIENT_BLOCK_INFO;
		vosMessage.bodyptr  = client_block_info;

		vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA, &vosMessage);
		if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Not able to post msg to WDA!",
				__func__);
			vos_mem_free(client_block_info);
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&pMac->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: sme_AcquireGlobalLock error!",
				__func__);
		vos_mem_free(client_block_info);
		status = eHAL_STATUS_FAILURE;
	}
	return status;
}
#endif /* SAP_AUTH_OFFLOAD */

#ifdef WLAN_FEATURE_APFIND
/**
 * sme_apfind_set_cmd() - set apfind configuration to firmware
 * @input: pointer to apfind request data.
 *
 * SME API to set APFIND configuations to firmware.
 *
 * Return: VOS_STATUS.
 */
VOS_STATUS sme_apfind_set_cmd(struct sme_ap_find_request_req *input)
{
     vos_msg_t msg;
     struct hal_apfind_request *data;
     size_t data_len;

     data_len = sizeof(struct hal_apfind_request) + input->request_data_len;
     data = vos_mem_malloc(data_len);

     if (data == NULL) {
         VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
                FL("Memory allocation failure"));
         return VOS_STATUS_E_FAULT;
     }

     vos_mem_zero(data, data_len);
     data->request_data_len = input->request_data_len;
     if (input->request_data_len) {
         vos_mem_copy(data->request_data,
                input->request_data, input->request_data_len);
     }

     msg.type = WDA_APFIND_SET_CMD;
     msg.reserved = 0;
     msg.bodyptr = data;

    if (VOS_STATUS_SUCCESS != vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)) {
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
            FL("Not able to post WDA_APFIND_SET_CMD message to WDA"));
        vos_mem_free(data);
        return VOS_STATUS_SUCCESS;
    }

     return VOS_STATUS_SUCCESS;
}
#endif /* WLAN_FEATURE_APFIND */
/**
 * sme_fw_mem_dump() - Get FW memory dump
 *
 * This API is invoked by HDD to indicate FW to start
 * dumping firmware memory.
 *
 * Return: eHalStatus
 */
#ifdef WLAN_FEATURE_MEMDUMP
eHalStatus sme_fw_mem_dump(tHalHandle hHal, void *recvd_req)
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	VOS_STATUS vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
	vos_msg_t msg;
	struct fw_dump_req* send_req;
	struct fw_dump_seg_req seg_req;
	int loop;

	send_req = vos_mem_malloc(sizeof(*send_req));
	if(!send_req) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("Memory allocation failed for WDA_FW_MEM_DUMP"));
		return eHAL_STATUS_FAILURE;
	}
	vos_mem_copy(send_req, recvd_req, sizeof(*send_req));

	VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
		  FL("request_id:%d num_seg:%d"),
		  send_req->request_id, send_req->num_seg);
        VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
		  FL("Segment Information"));
	for (loop = 0; loop < send_req->num_seg; loop++) {
		seg_req = send_req->segment[loop];
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
			  FL("seg_number:%d"), loop);
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
			  FL("seg_id:%d start_addr_lo:0x%x start_addr_hi:0x%x"),
			  seg_req.seg_id, seg_req.seg_start_addr_lo,
			  seg_req.seg_start_addr_hi);
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
			  FL("seg_length:%d dst_addr_lo:0x%x dst_addr_hi:0x%x"),
			  seg_req.seg_length, seg_req.dst_addr_lo,
			  seg_req.dst_addr_hi);
	}

	if (eHAL_STATUS_SUCCESS == sme_AcquireGlobalLock(&pMac->sme)) {
		msg.bodyptr = send_req;
		msg.type = WDA_FW_MEM_DUMP_REQ;
		msg.reserved = 0;

		vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA, &msg);
		if (VOS_STATUS_SUCCESS != vos_status) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				  FL("Not able to post WDA_FW_MEM_DUMP"));
			vos_mem_free(send_req);
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&pMac->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("Failed to acquire SME Global Lock"));
		vos_mem_free(send_req);
		status = eHAL_STATUS_FAILURE;
	}

	return status;
}
#else
eHalStatus sme_fw_mem_dump(tHalHandle hHal, void *recvd_req)
{
	return eHAL_STATUS_SUCCESS;
}
#endif /* WLAN_FEATURE_MEMDUMP */
#ifdef FEATURE_WLAN_MCC_TO_SCC_SWITCH
/*
 * sme_validate_sap_channel_switch() - validate target channel switch w.r.t
 *      concurreny rules set to avoid channel interference.
 * @hal - Hal context
 * @sap_phy_mode - phy mode of SAP
 * @cc_switch_mode - concurreny switch mode
 * @session_id - sme session id.
 *
 * Return: true if there is no channel interference else return false
 */
bool sme_validate_sap_channel_switch(tHalHandle hal,
                   uint16_t sap_ch,
                   eCsrPhyMode sap_phy_mode,
                   uint8_t cc_switch_mode,
                   uint32_t session_id)
{
	eHalStatus status = eHAL_STATUS_FAILURE;
	tpAniSirGlobal mac = PMAC_STRUCT(hal);
	tCsrRoamSession *session = CSR_GET_SESSION(mac, session_id);
	uint16_t intf_channel = 0;

	if (!session)
		return false;
	session->ch_switch_in_progress = true;
	status = sme_AcquireGlobalLock(&mac->sme);
	if (HAL_STATUS_SUCCESS(status))	{
		intf_channel = csrCheckConcurrentChannelOverlap(mac, sap_ch,
						sap_phy_mode,
						cc_switch_mode);
		sme_ReleaseGlobalLock(&mac->sme);
	} else {
		smsLog(mac, LOGE, FL(" sme_AcquireGlobalLock error!"));
		session->ch_switch_in_progress = false;
		return false;
	}

	session->ch_switch_in_progress = false;
	return (intf_channel == 0)? true : false;
}
#endif

/**
 * sme_configure_stats_avg_factor() - function to config avg. stats factor
 * @hHal: hHal
 * @session_id: session ID
 * @stats_avg_factor: average stats factor
 *
 * This function configures the avg stats factor in firmware
 *
 * Return: eHalStatus
 */
eHalStatus sme_configure_stats_avg_factor(tHalHandle hHal, tANI_U8 session_id,
                                          tANI_U16 stats_avg_factor)
{
	vos_msg_t msg;
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
	struct sir_stats_avg_factor *stats_factor;

	stats_factor = vos_mem_malloc(sizeof(*stats_factor));

	if (!stats_factor) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  "%s: Not able to allocate memory for WDA_SET_MDNS_RESPONSE_CMD",
			  __func__);
		return eHAL_STATUS_E_MALLOC_FAILED;
	}

	status = sme_AcquireGlobalLock(&pMac->sme);

	if (eHAL_STATUS_SUCCESS == status) {

		stats_factor->vdev_id = session_id;
		stats_factor->stats_avg_factor = stats_avg_factor;

		/* serialize the req through MC thread */
		msg.type     = SIR_HAL_CONFIG_STATS_FACTOR;
		msg.bodyptr  = stats_factor;

		if (!VOS_IS_STATUS_SUCCESS(
			    vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))) {
			VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				   "%s: Not able to post SIR_HAL_CONFIG_STATS_FACTOR to WMA!",
				   __func__);
			vos_mem_free(stats_factor);
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&pMac->sme);
	} else {
		VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			   "%s: sme_AcquireGlobalLock error!",
			   __func__);
		vos_mem_free(stats_factor);
	}

	return status;
}

/**
 * sme_configure_guard_time() - function to configure guard time
 * @hHal:   SME API to enable/disable DFS channel scan
 * @session_id: session ID
 * @guard_time: Guard time
 *
 * This function configures the guard time in firmware
 *
 * Return: eHalStatus
 */
eHalStatus sme_configure_guard_time(tHalHandle hHal, tANI_U8 session_id,
                                    tANI_U32 guard_time)
{
	vos_msg_t msg;
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal pMac  = PMAC_STRUCT(hHal);
	struct sir_guard_time_request *g_time;

	g_time = vos_mem_malloc(sizeof(*g_time));

	if (!g_time) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  "%s: Not able to allocate memory for SIR_HAL_CONFIG_GUARD_TIME",
			  __func__);
		return eHAL_STATUS_E_MALLOC_FAILED;
	}

	status = sme_AcquireGlobalLock(&pMac->sme);

	if (eHAL_STATUS_SUCCESS == status) {

		g_time->vdev_id = session_id;
		g_time->guard_time = guard_time;

		/* serialize the req through MC thread */
		msg.type     = SIR_HAL_CONFIG_GUARD_TIME;
		msg.bodyptr  = g_time;

		if (!VOS_IS_STATUS_SUCCESS(
			    vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))) {
			VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				   "%s: Not able to post SIR_HAL_CONFIG_GUARD_TIME to WDA!",
				   __func__);
			vos_mem_free(g_time);
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&pMac->sme);
	} else {
		VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			   "%s: sme_AcquireGlobalLock error!",
			   __func__);
		vos_mem_free(g_time);
	}

	return status;
}

/**
 * sme_update_roam_scan_hi_rssi_scan_params() - update high rssi scan
 *         params
 * @hal_handle - The handle returned by macOpen.
 * @session_id - Session Identifier
 * @notify_id - Identifies 1 of the 4 parameters to be modified
 * @val New value of the parameter
 *
 * Return: eHAL_STATUS_SUCCESS - SME update config successful.
 *         Other status means SME failed to update
 */

eHalStatus sme_update_roam_scan_hi_rssi_scan_params(tHalHandle hal_handle,
	uint8_t session_id,
	uint32_t notify_id,
	int32_t val)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal_handle);
	eHalStatus status  = eHAL_STATUS_SUCCESS;
	tCsrNeighborRoamConfig *nr_config = NULL;
	tpCsrNeighborRoamControlInfo nr_info = NULL;
	uint32_t reason = 0;

	status = sme_AcquireGlobalLock(&mac_ctx->sme);
	if (HAL_STATUS_SUCCESS(status)) {
		nr_config = &mac_ctx->roam.configParam.neighborRoamConfig;
		nr_info   = &mac_ctx->roam.neighborRoamInfo[session_id];
		switch (notify_id) {
		case eCSR_HI_RSSI_SCAN_MAXCOUNT_ID:
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
				"%s: gRoamScanHirssiMaxCount is changed from %d to %d",
				__func__, nr_config->nhi_rssi_scan_max_count,
				val);
			nr_config->nhi_rssi_scan_max_count = val;
			nr_info->cfgParams.hi_rssi_scan_max_count = val;
			reason = REASON_ROAM_SCAN_HI_RSSI_MAXCOUNT_CHANGED;
		break;

		case eCSR_HI_RSSI_SCAN_RSSI_DELTA_ID:
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
				"%s: gRoamScanHiRssiDelta is changed from %d to %d",
				__func__, nr_config->nhi_rssi_scan_rssi_delta,
				val);
			nr_config->nhi_rssi_scan_rssi_delta = val;
			nr_info->cfgParams.hi_rssi_scan_rssi_delta = val;
			reason = REASON_ROAM_SCAN_HI_RSSI_DELTA_CHANGED;
			break;

		case eCSR_HI_RSSI_SCAN_DELAY_ID:
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
				"%s: gRoamScanHiRssiDelay is changed from %d to %d",
				__func__, nr_config->nhi_rssi_scan_delay,
				val);
			nr_config->nhi_rssi_scan_delay = val;
			nr_info->cfgParams.hi_rssi_scan_delay = val;
			reason = REASON_ROAM_SCAN_HI_RSSI_DELAY_CHANGED;
			break;

		case eCSR_HI_RSSI_SCAN_RSSI_UB_ID:
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
				"%s: gRoamScanHiRssiUpperBound is changed from %d to %d",
				__func__,
				nr_config->nhi_rssi_scan_rssi_ub,
				val);
			nr_config->nhi_rssi_scan_rssi_ub = val;
			nr_info->cfgParams.hi_rssi_scan_rssi_ub = val;
			reason = REASON_ROAM_SCAN_HI_RSSI_UB_CHANGED;
			break;

		default:
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: invalid parameter notify_id %d", __func__,
				notify_id);
			status = eHAL_STATUS_INVALID_PARAMETER;
			break;
		}
		sme_ReleaseGlobalLock(&mac_ctx->sme);
	}
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
	if (mac_ctx->roam.configParam.isRoamOffloadScanEnabled &&
		status == eHAL_STATUS_SUCCESS) {
		csrRoamOffloadScan(mac_ctx, session_id,
			ROAM_SCAN_OFFLOAD_UPDATE_CFG, reason);
	}
#endif

	return status;
}

/**
 * sme_configure_modulated_dtim() - function to configure modulated dtim
 * @h_hal: SME API to enable/disable modulated DTIM instantaneously
 * @session_id: session ID
 * @modulated_dtim: modulated dtim value
 *
 * This function configures the guard time in firmware
 *
 * Return: eHalStatus
 */
eHalStatus sme_configure_modulated_dtim(tHalHandle h_hal, tANI_U8 session_id,
				      tANI_U32 modulated_dtim)
{
	vos_msg_t msg;
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal pMac  = PMAC_STRUCT(h_hal);
	wda_cli_set_cmd_t *iwcmd;

	iwcmd = vos_mem_malloc(sizeof(*iwcmd));
	if (NULL == iwcmd) {
		VOS_TRACE(VOS_MODULE_ID_SME,
			  VOS_TRACE_LEVEL_FATAL,
			  "%s: vos_mem_alloc failed", __func__);
		return eHAL_STATUS_FAILED_ALLOC;
	}

	status = sme_AcquireGlobalLock(&pMac->sme);

	if (eHAL_STATUS_SUCCESS == status) {

		vos_mem_zero((void *)iwcmd, sizeof(*iwcmd));
		iwcmd->param_value = modulated_dtim;
		iwcmd->param_vdev_id = session_id;
		iwcmd->param_id = GEN_PARAM_MODULATED_DTIM;
		iwcmd->param_vp_dev = GEN_CMD;
		msg.type = WDA_CLI_SET_CMD;
		msg.reserved = 0;
		msg.bodyptr = (void *)iwcmd;

		if (!VOS_IS_STATUS_SUCCESS(
			    vos_mq_post_message(VOS_MODULE_ID_WDA, &msg))) {
			VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				   "%s: Not able to post SIR_HAL_CONFIG_GUARD_TIME to WDA!",
				   __func__);
			vos_mem_free(iwcmd);
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&pMac->sme);
	} else {
		VOS_TRACE( VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			   "%s: sme_AcquireGlobalLock error!",
			   __func__);
		vos_mem_free(iwcmd);
	}

	return status;
}

/**
 * sme_set_tsfcb() - set callback which to handle WMI_VDEV_TSF_REPORT_EVENTID
 *
 * @hHal: Handler return by macOpen.
 * @pcallbackfn: callback to handle the tsf event
 * @pcallbackcontext: callback context
 *
 * Return: eHalStatus.
 */
eHalStatus sme_set_tsfcb
(
	tHalHandle hHal,
	int (*pcallbackfn)(void *pcallbackcontext, struct stsf *pTsf),
	void *pcallbackcontext
)
{
	tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
	eHalStatus status;

	status = sme_AcquireGlobalLock(&pMac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		pMac->sme.get_tsf_cb = pcallbackfn;
		pMac->sme.get_tsf_cxt = pcallbackcontext;
		sme_ReleaseGlobalLock(&pMac->sme);
	}
	return status;
}

#ifdef WLAN_FEATURE_TSF
/*
 * sme_set_tsf_gpio() - set gpio pin that be toggled when capture tef
 *
 * @hHal: Handler return by macOpen
 * @pinvalue: gpio pin id
 *
 * Return: eHalStatus
 */
eHalStatus sme_set_tsf_gpio(tHalHandle hHal, uint32_t pinvalue)
{
	eHalStatus status;
	VOS_STATUS vos_status;
	vos_msg_t vos_msg = {0};
	tpAniSirGlobal pMac = PMAC_STRUCT(hHal);

	status = sme_AcquireGlobalLock(&pMac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		vos_msg.type = WDA_TSF_GPIO_PIN;
		vos_msg.reserved = 0;
		vos_msg.bodyval = pinvalue;

		vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_msg);
		if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: not able to post WDA_TSF_GPIO_PIN",
				__func__);
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&pMac->sme);
	}
	return status;
}
#endif

/*
 * sme_wifi_start_logger() - This function send the command to WMA
 * to either start/stop logging
 * @hal: HAL context
 * @start_log: Structure containing the wifi start logger params
 *
 * This function send the command to WMA to either start/stop logging
 *
 * Return: eHAL_STATUS_SUCCESS on successful posting
 */
eHalStatus sme_wifi_start_logger(tHalHandle hal,
		struct sir_wifi_start_log start_log)
{
	eHalStatus status     = eHAL_STATUS_SUCCESS;
	VOS_STATUS vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal mac   = PMAC_STRUCT(hal);
	vos_msg_t vos_message;
	struct sir_wifi_start_log *req_msg;
	uint32_t len;

	len = sizeof(*req_msg);
	req_msg = vos_mem_malloc(len);
	if (!req_msg) {
		smsLog(mac, LOGE, FL("vos_mem_malloc failed"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	vos_mem_zero(req_msg, len);

	req_msg->verbose_level = start_log.verbose_level;
	req_msg->flag = start_log.flag;
	req_msg->ring_id = start_log.ring_id;

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
				FL("sme_AcquireGlobalLock failed!(status=%d)"),
				status);
		vos_mem_free(req_msg);
		return status;
	}

	/* Serialize the req through MC thread */
	vos_message.bodyptr = req_msg;
	vos_message.type    = SIR_HAL_START_STOP_LOGGING;
	vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		smsLog(mac, LOGE,
				FL("vos_mq_post_message failed!(err=%d)"),
				vos_status);
		vos_mem_free(req_msg);
		status = eHAL_STATUS_FAILURE;
	}
	sme_ReleaseGlobalLock(&mac->sme);
	return status;
}

/**
 * smeNeighborMiddleOfRoaming() - Function to know if
 * STA is in the middle of roaming states
 * @hal:                Handle returned by macOpen
 * @sessionId: sessionId of the STA session
 *
 * This function is a wrapper to call
 * csrNeighborMiddleOfRoaming to know if
 * STA is in the middle of roaming states
 *
 * Return: True or False
 *
 */
bool smeNeighborMiddleOfRoaming(tHalHandle hHal, tANI_U8 sessionId)
{
	return csrNeighborMiddleOfRoaming(PMAC_STRUCT(hHal), sessionId);
}

/**
 * sme_update_nss() - SME API to change the number for spatial streams (1 or 2)
 * @hal:            - Handle returned by macOpen
 * @nss:            - Number of spatial streams
 *
 * This function is used to update the number of spatial streams supported.
 *
 * Return: Success upon successfully changing nss else failure
 *
 */
eHalStatus sme_update_nss(tHalHandle h_hal, uint8_t nss)
{
	eHalStatus status;
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(h_hal);
	uint32_t  i, value = 0;
	union {
		uint16_t                        cfg_value16;
		tSirMacHTCapabilityInfo         ht_cap_info;
	} uHTCapabilityInfo;
	tCsrRoamSession *csr_session;

	status = sme_AcquireGlobalLock(&mac_ctx->sme);

	if (eHAL_STATUS_SUCCESS == status) {
		mac_ctx->roam.configParam.enable2x2 = (nss == 1) ? 0 : 1;

		/* get the HT capability info*/
		ccmCfgGetInt(mac_ctx, WNI_CFG_HT_CAP_INFO, &value);
		uHTCapabilityInfo.cfg_value16 = (0xFFFF & value);

		for (i = 0; i < CSR_ROAM_SESSION_MAX; i++) {
			if (CSR_IS_SESSION_VALID(mac_ctx, i)) {
				csr_session = &mac_ctx->roam.roamSession[i];
				csr_session->htConfig.ht_tx_stbc =
					uHTCapabilityInfo.ht_cap_info.txSTBC;
			}
		}

		sme_ReleaseGlobalLock(&mac_ctx->sme);
	}

	return status;
}

uint8_t sme_is_any_session_in_connected_state(tHalHandle h_hal)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(h_hal);
	eHalStatus     status;
	uint8_t        ret     = FALSE;

	status = sme_AcquireGlobalLock(&mac_ctx->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		ret = csrIsAnySessionInConnectState(mac_ctx);
		sme_ReleaseGlobalLock(&mac_ctx->sme);
	}
	return ret;
}

/**
 * vos_send_flush_logs_cmd_to_fw() - Flush FW logs
 * @mac: MAC handle
 *
 * This function is used to send the command that will
 * be used to flush the logs in the firmware
 *
 * Return: eHalStatus
 */
eHalStatus vos_send_flush_logs_cmd_to_fw(tpAniSirGlobal mac)
{
	eHalStatus status;
	VOS_STATUS vos_status;
	vos_msg_t vos_message;

	status = sme_AcquireGlobalLock(&mac->sme);
	if (status != eHAL_STATUS_SUCCESS) {
		smsLog(mac, LOGE,
			FL("sme_AcquireGlobalLock failed!(status=%d)"),
			status);
		return status;
	}

	/* Serialize the req through MC thread */
	vos_message.bodyptr = NULL;
	vos_message.type    = SIR_HAL_FLUSH_LOG_TO_FW;
	vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		smsLog(mac, LOGE,
			FL("vos_mq_post_message failed!(err=%d)"),
			vos_status);
		status = eHAL_STATUS_FAILURE;
	}
	sme_ReleaseGlobalLock(&mac->sme);
	return status;
}

/**
 * sme_handle_set_fcc_channel() - set specific txPower for non-fcc channel
 * @hal: HAL pointer
 * @fcc_constraint: true: disable, false; enable
 *
 * Return: eHalStatus.
 */
eHalStatus sme_handle_set_fcc_channel(tHalHandle hal, bool fcc_constraint)
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal mac_ptr  = PMAC_STRUCT(hal);

	status = sme_AcquireGlobalLock(&mac_ptr->sme);

	if (eHAL_STATUS_SUCCESS == status) {

		if (fcc_constraint != mac_ptr->scan.fcc_constraint) {
			mac_ptr->scan.fcc_constraint = fcc_constraint;

			/* update the channel list to the firmware */
			status = csrUpdateChannelList(mac_ptr);
		}

		sme_ReleaseGlobalLock(&mac_ptr->sme);
	}

	return status;
}
/**
 * sme_enable_phy_error_logs() - Enable DFS phy error logs
 * @hal:        global hal handle
 * @enable_log: value to set
 *
 * Since the frequency of DFS phy error is very high, enabling logs for them
 * all the times can cause crash and will also create lot of useless logs
 * causing difficulties in debugging other issue. This function will be called
 * from iwpriv cmd to eanble such logs temporarily.
 *
 * Return: void
 */
void sme_enable_phy_error_logs(tHalHandle hal, bool enable_log)
{
    tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
    mac_ctx->sap.enable_dfs_phy_error_logs = enable_log;
}

/**
 * sme_SetDefDot11Mode() - Updates pMac with default dot11mode
 * @hal: Global MAC pointer
 *
 * Return: NULL.
 */
void sme_SetDefDot11Mode(tHalHandle hal)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT( hal );
	csrSetDefaultDot11Mode(mac_ctx);
}

/**
 * sme_update_user_configured_nss() - sets the nss based on user request
 * @hal: Pointer to HAL
 * @nss: number of streams
 *
 * Return: None
 */
void sme_update_user_configured_nss(tHalHandle hal, uint8_t nss)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	mac_ctx->user_configured_nss = nss;
}

/**
 * sme_set_vdev_nss() - sets the vdev nss based on INI
 * @hal: Pointer to HAL
 * @enable2x2: 1x1 or 2x2 mode.
 *
 * Sets the per band Nss for each vdev type based on INI.
 *
 * Return: None
 */
void sme_set_vdev_nss(tHalHandle hal, bool enable2x2)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	struct vdev_type_nss *vdev_nss;
	uint8_t i;
	uint8_t nss_val;
	uint8_t coex;

	if (enable2x2) {
		if (mac_ctx->lteCoexAntShare)
			coex = 1;
		else
			coex = 0;
		nss_val = 2;
	} else {
		nss_val = 1;
		coex = 0;
	}
	mac_ctx->user_configured_nss = nss_val;

	vdev_nss = &mac_ctx->vdev_type_nss_2g;

	for (i = 0; i < NUM_OF_BANDS; i++) {
		vdev_nss->sta = nss_val;
		vdev_nss->sap = nss_val - coex;
		vdev_nss->p2p_go = nss_val - coex;
		vdev_nss->p2p_cli = nss_val - coex;
		vdev_nss->p2p_dev = nss_val - coex;
		vdev_nss->ibss = nss_val - coex;
		vdev_nss->tdls = nss_val - coex;
		vdev_nss->ocb = nss_val - coex;

		vdev_nss = &mac_ctx->vdev_type_nss_5g;
		coex = 0;
	}
}

/**
 * sme_update_vdev_type_nss() - sets the nss per vdev type
 * @hal: Pointer to HAL
 * @max_supp_nss: max_supported Nss
 * @band: 5G or 2.4G band
 *
 * Sets the per band Nss for each vdev type based on INI and configured
 * chain mask value.
 *
 * Return: None
 */
void sme_update_vdev_type_nss(tHalHandle hal, uint8_t max_supp_nss,
		uint32_t vdev_type_nss, eCsrBand band)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	struct vdev_type_nss *vdev_nss;

	if (eCSR_BAND_5G == band) {
		vdev_nss = &mac_ctx->vdev_type_nss_5g;
	} else {
		vdev_nss = &mac_ctx->vdev_type_nss_2g;
	}

	vdev_nss->sta = VOS_MIN(max_supp_nss, CFG_STA_NSS(vdev_type_nss));
	vdev_nss->sap = VOS_MIN(max_supp_nss, CFG_SAP_NSS(vdev_type_nss));
	vdev_nss->p2p_go = VOS_MIN(max_supp_nss,
				CFG_P2P_GO_NSS(vdev_type_nss));
	vdev_nss->p2p_cli = VOS_MIN(max_supp_nss,
				CFG_P2P_CLI_NSS(vdev_type_nss));
	vdev_nss->p2p_dev = VOS_MIN(max_supp_nss,
				CFG_P2P_DEV_NSS(vdev_type_nss));
	vdev_nss->ibss = VOS_MIN(max_supp_nss, CFG_IBSS_NSS(vdev_type_nss));
	vdev_nss->tdls = VOS_MIN(max_supp_nss, CFG_TDLS_NSS(vdev_type_nss));
	vdev_nss->ocb = VOS_MIN(max_supp_nss, CFG_OCB_NSS(vdev_type_nss));
	mac_ctx->user_configured_nss = max_supp_nss;

	smsLog(mac_ctx, LOG1,
           "band %d NSS: sta %d sap %d cli %d go %d dev %d ibss %d tdls %d ocb %d",
           band, vdev_nss->sta, vdev_nss->sap, vdev_nss->p2p_cli,
           vdev_nss->p2p_go, vdev_nss->p2p_dev, vdev_nss->ibss,
           vdev_nss->tdls, vdev_nss->ocb);
}

/**
 * sme_set_per_band_chainmask_supp() - sets the per band chainmask support
 * @hal: Pointer to HAL
 * @val: Value to be set.
 *
 * Sets the per band chain mask support to mac context.
 * Return: None
 */
void sme_set_per_band_chainmask_supp(tHalHandle hal, bool val)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	mac_ctx->per_band_chainmask_supp = val;
}

/**
 * sme_set_lte_coex_supp() - sets the lte coex antenna share support
 * @hal: Pointer to HAL
 * @val: Value to be set.
 *
 * Sets the lte coex antenna share support to mac context.
 * Return: None
 */
void sme_set_lte_coex_supp(tHalHandle hal, bool val)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	mac_ctx->lteCoexAntShare = val;
}

/**
 * sme_set_bcon_offload_supp() - sets the beacon offload support
 * @hal: Pointer to HAL
 * @val: Value to be set.
 *
 * Sets the beacon offload support to mac context.
 * Return: None
 */
void sme_set_bcon_offload_supp(tHalHandle hal, bool val)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	mac_ctx->beacon_offload = val;
}

#ifdef FEATURE_WLAN_TDLS

/**
 * sme_get_opclass() - determine operating class
 * @hal: Pointer to HAL
 * @channel: channel id
 * @bw_offset: bandwidth offset
 * @opclass: pointer to operating class
 *
 * Function will determine operating class from regdm_get_opclass_from_channel
 *
 * Return: none
 */
void sme_get_opclass(tHalHandle hal, uint8_t channel, uint8_t bw_offset,
			uint8_t *opclass)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);

	/* redgm opclass table contains opclass for 40MHz low primary,
	 * 40MHz high primary and 20MHz. No support for 80MHz yet. So
	 * first we will check if bit for 40MHz is set and if so find
	 * matching opclass either with low primary or high primary
	 * (a channel would never be in both) and then search for opclass
	 * matching 20MHz, else for any BW.
	 */
	if (bw_offset & (1 << BW_40_OFFSET_BIT)) {
		*opclass = regdm_get_opclass_from_channel(
				mac_ctx->scan.countryCodeCurrent,
				channel,
				BW40_LOW_PRIMARY);
		if (!(*opclass)) {
			*opclass = regdm_get_opclass_from_channel(
					mac_ctx->scan.countryCodeCurrent,
					channel,
					BW40_HIGH_PRIMARY);
		}
	} else if (bw_offset & (1 << BW_20_OFFSET_BIT)) {
		*opclass = regdm_get_opclass_from_channel(
				mac_ctx->scan.countryCodeCurrent,
				channel,
				BW20);
	} else {
		*opclass = regdm_get_opclass_from_channel(
				mac_ctx->scan.countryCodeCurrent,
				channel,
				BWALL);
	}
}

#endif



#ifdef WLAN_FEATURE_UDP_RESPONSE_OFFLOAD
/**
 * sme_set_udp_resp_offload() - set udp response payload.
 * @pudp_resp_cmd: specific udp and response udp payload struct pointer
 *
 * This function set specific udp and response udp payload info
 * including enable dest_port,udp_payload, resp_payload.
 *
 * Return: Return VOS_STATUS.
 */
VOS_STATUS sme_set_udp_resp_offload(struct udp_resp_offload *pudp_resp_cmd)
{
	vos_msg_t vos_message;
	VOS_STATUS vos_status;
	struct udp_resp_offload *udp_resp_cmd;

	if (!pudp_resp_cmd) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: invalid pudp_resp_cmd pointer", __func__);
		return VOS_STATUS_E_FAILURE;
	}

	udp_resp_cmd = vos_mem_malloc(sizeof(*udp_resp_cmd));
	if (NULL == udp_resp_cmd) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: fail to alloc sudp_cmd", __func__);
		return VOS_STATUS_E_FAILURE;
	}

	*udp_resp_cmd = *pudp_resp_cmd;

	vos_message.type = WDA_SET_UDP_RESP_OFFLOAD;
	vos_message.bodyptr = udp_resp_cmd;
	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA,
					&vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: Not able to post msg to WDA!",
				__func__);
		vos_mem_free(udp_resp_cmd);
		vos_status = VOS_STATUS_E_FAILURE;
	}

	return vos_status;
}
#endif

/**
 * sme_set_lost_link_info_cb() - plug in callback function for receiving
 * lost link info
 * @hal: HAL handle
 * @cb: callback function
 *
 * Return: HAL status
 */
eHalStatus sme_set_lost_link_info_cb(tHalHandle hal,
				     void (*cb)(void *,
				     struct sir_lost_link_info *))
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal mac = PMAC_STRUCT(hal);

	status = sme_AcquireGlobalLock(&mac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		mac->sme.lost_link_info_cb = cb;
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
			  "%s: set lost link info callback", __func__);
		sme_ReleaseGlobalLock(&mac->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  "%s: sme_AcquireGlobalLock error status %d",
			  __func__, status);
	}
	return status;
}

/**
 * sme_set_smps_force_mode_cb() - callback set by HDD for smps
 * force mode event
 * @hal: The handle returned by macOpen
 * @cb: callback function
 *
 * Return: eHAL_STATUS_SUCCESS if callback was set successfully
 * else failure status
 */
eHalStatus sme_set_smps_force_mode_cb(tHalHandle hal,
				void (*cb)(void *,
				struct sir_smps_force_mode_event *))
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal mac = PMAC_STRUCT(hal);

	status = sme_AcquireGlobalLock(&mac->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		mac->sme.smps_force_mode_cb = cb;
		sme_ReleaseGlobalLock(&mac->sme);
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_INFO,
			"%s: set smps force mode callback", __func__);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  "%s: sme_AcquireGlobalLock error status %d",
			  __func__, status);
	}
	return status;
}

#ifdef WLAN_FEATURE_WOW_PULSE
/**
 * sme_set_wow_pulse() - set wow pulse info
 * @wow_pulse_set_info: wow_pulse_mode structure pointer
 *
 * Return: HAL status
 */
VOS_STATUS sme_set_wow_pulse(struct wow_pulse_mode *wow_pulse_set_info)
{
	vos_msg_t vos_message;
	VOS_STATUS vos_status;
	struct wow_pulse_mode *wow_pulse_set_cmd;

	if (!wow_pulse_set_info) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			"%s: invalid wow_pulse_set_info pointer", __func__);
		return VOS_STATUS_E_FAILURE;
	}

	wow_pulse_set_cmd = vos_mem_malloc(sizeof(*wow_pulse_set_cmd));
	if (NULL == wow_pulse_set_cmd) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			"%s: fail to alloc wow_pulse_set_cmd", __func__);
		return VOS_STATUS_E_NOMEM;
	}

	*wow_pulse_set_cmd = *wow_pulse_set_info;

	vos_message.type = WDA_SET_WOW_PULSE_CMD;
	vos_message.bodyptr = wow_pulse_set_cmd;
	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA,
					&vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			"%s: Not able to post msg to WDA!",
			__func__);
		vos_mem_free(wow_pulse_set_cmd);
		vos_status = VOS_STATUS_E_FAILURE;
	}

	return vos_status;
}
#endif

#ifdef FEATURE_GREEN_AP
/**
 * sme_send_egap_conf_params() - set the enhanced green ap configuration params
 *
 * @enable: enable/disable the enhanced green ap feature
 * @inactivity_time: inactivity timeout value
 * @wait_time: wait timeout value
 * @flag: feature flag in bitmasp
 *
 * Return: Return VOS_STATUS, otherwise appropriate failure code
 */
VOS_STATUS sme_send_egap_conf_params(uint32_t enable, uint32_t inactivity_time,
				     uint32_t wait_time, uint32_t flags)
{
	vos_msg_t vos_message;
	VOS_STATUS vos_status;
	struct egap_conf_params *egap_params;

	egap_params = vos_mem_malloc(sizeof(*egap_params));
	if (NULL == egap_params) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: fail to alloc egap_params", __func__);
		return VOS_STATUS_E_FAILURE;
	}

	egap_params->enable = enable;
	egap_params->inactivity_time = inactivity_time;
	egap_params->wait_time = wait_time;
	egap_params->flags = flags;

	vos_message.type = WDA_SET_EGAP_CONF_PARAMS;
	vos_message.bodyptr = egap_params;
	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA,
					&vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			"%s: Not able to post msg to WDA!",
			__func__);

		vos_mem_free(egap_params);
	}
	return vos_status;
}
#endif

/**
 * sme_update_mimo_power_save() - Update MIMO power save
 * configuration
 * @hal: The handle returned by macOpen
 * @is_ht_smps_enabled: enable/disable ht smps
 * @ht_smps_mode: smps mode disabled/static/dynamic
 *
 * Return: eHAL_STATUS_SUCCESS if SME update mimo power save
 * configuration sucsess else failue status
 */
eHalStatus sme_update_mimo_power_save(tHalHandle hal,
				      uint8_t is_ht_smps_enabled,
				      uint8_t ht_smps_mode)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	eHalStatus status;

	status = sme_AcquireGlobalLock(&mac_ctx->sme);
	if (HAL_STATUS_SUCCESS(status)) {
		smsLog(mac_ctx, LOG1,
		       "update mimo power save config enable smps: %d smps mode: %d",
		       is_ht_smps_enabled, ht_smps_mode);
		mac_ctx->roam.configParam.enableHtSmps =
			is_ht_smps_enabled;
		mac_ctx->roam.configParam.htSmps = ht_smps_mode;
		sme_ReleaseGlobalLock(&mac_ctx->sme);
	}
	return status;
}

/**
 * sme_is_sta_smps_allowed() - check if the supported nss for
 * the session is greater than 1x1 to enable sta SMPS
 * @hal: The handle returned by macOpen
 * @session_id: session id
 *
 * Return: bool returns true if supported nss is greater than
 * 1x1 else false
 */
bool sme_is_sta_smps_allowed(tHalHandle hal, uint8_t session_id)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	tCsrRoamSession *csr_session;

	if (!mac_ctx) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Failed to get mac_ctx"));
		return false;
	}

	csr_session = CSR_GET_SESSION(mac_ctx, session_id);
	if (NULL == csr_session) {
		smsLog(mac_ctx, LOGE, "SME session not valid: %d", session_id);
		return false;
	}

	if (!CSR_IS_SESSION_VALID(mac_ctx, session_id)) {
		smsLog(mac_ctx, LOGE, "CSR session not valid: %d", session_id);
		return false;
	}

	return (csr_session->supported_nss_1x1 == true) ? false : true;
}

/**
 * sme_get_bpf_offload_capabilities() - Get length for BPF offload
 * @hal: Global HAL handle
 * This function constructs the vos message and fill in message type,
 * post the same to WDA.
 * Return: eHalstatus enumeration
 */
eHalStatus sme_get_bpf_offload_capabilities(tHalHandle hal)
{
	eHalStatus          status    = eHAL_STATUS_SUCCESS;
	VOS_STATUS          vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal      mac_ctx      = PMAC_STRUCT(hal);
	vos_msg_t           vos_msg;

	smsLog(mac_ctx, LOG1, FL("enter"));

	status = sme_AcquireGlobalLock(&mac_ctx->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		/* Serialize the req through MC thread */
		vos_msg.bodyptr = NULL;
		vos_msg.type = WDA_BPF_GET_CAPABILITIES_REQ;
		vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_msg);
		if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
					FL("Post bpf get offload msg fail"));
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&mac_ctx->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("sme_AcquireGlobalLock error"));
	}
	smsLog(mac_ctx, LOG1, FL("exit"));
	return status;
}


/**
 * sme_set_bpf_instructions() - Set BPF bpf filter instructions.
 * @hal: HAL handle
 * @bpf_set_offload: struct to set bpf filter instructions.
 *
 * Return: eHalStatus enumeration.
 */
eHalStatus sme_set_bpf_instructions(tHalHandle hal,
				    struct sir_bpf_set_offload *req)
{
	eHalStatus          status     = eHAL_STATUS_SUCCESS;
	VOS_STATUS          vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal      mac_ctx    = PMAC_STRUCT(hal);
	vos_msg_t           vos_msg;
	struct sir_bpf_set_offload *set_offload;

	set_offload = vos_mem_malloc(sizeof(*set_offload));

	if (NULL == set_offload) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("Failed to alloc set_offload"));
		return eHAL_STATUS_FAILED_ALLOC;
	}
	vos_mem_zero(set_offload, sizeof(*set_offload));

	set_offload->session_id = req->session_id;
	set_offload->filter_id = req->filter_id;
	set_offload->current_offset = req->current_offset;
	set_offload->total_length = req->total_length;
	set_offload->current_length = req->current_length;
	if (set_offload->total_length) {
		set_offload->program = vos_mem_malloc(sizeof(uint8_t) *
						req->current_length);
		vos_mem_copy(set_offload->program, req->program,
				set_offload->current_length);
	}
	status = sme_AcquireGlobalLock(&mac_ctx->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		/* Serialize the req through MC thread */
		vos_msg.bodyptr = set_offload;
		vos_msg.type = WDA_BPF_SET_INSTRUCTIONS_REQ;
		vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_msg);

		if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				FL("Post BPF set offload msg fail"));
			status = eHAL_STATUS_FAILURE;
			if (set_offload->total_length)
				vos_mem_free(set_offload->program);
			vos_mem_free(set_offload);
		}
		sme_ReleaseGlobalLock(&mac_ctx->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				FL("sme_AcquireGlobalLock failed"));
		if (set_offload->total_length)
			vos_mem_free(set_offload->program);
		vos_mem_free(set_offload);
	}
	return status;
}

/**
 * sme_bpf_offload_register_callback() - Register get bpf offload callbacK
 *
 * @hal - MAC global handle
 * @callback_routine - callback routine from HDD
 *
 * This API is invoked by HDD to register its callback in SME
 *
 * Return: eHalStatus
 */
eHalStatus sme_bpf_offload_register_callback(tHalHandle hal,
				void (*pbpf_get_offload_cb)(void *context,
					struct sir_bpf_get_offload *))
{
	eHalStatus status   = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal mac  = PMAC_STRUCT(hal);

	status = sme_AcquireGlobalLock(&mac->sme);
	if (HAL_STATUS_SUCCESS(status)) {
		mac->sme.pbpf_get_offload_cb = pbpf_get_offload_cb;
		sme_ReleaseGlobalLock(&mac->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("sme_AcquireGlobalLock failed"));
	}
	return status;
}

/**
 * sme_set_mib_stats_enable() - sme function to set ini parms to FW.
 * @hal_handle: reference to the HAL
 * @value: enable/disable
 *
 * This function sends mib stats enable/disable command to vos
 *
 * Return: hal_status
 */
eHalStatus sme_set_mib_stats_enable(tHalHandle hal_handle, uint8_t value)
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	vos_msg_t vos_msg;

	vos_msg.bodyptr = NULL;

	if (value)
		vos_msg.type = WDA_SET_MIB_STATS_ENABLE;
	else
		vos_msg.type = WDA_SET_MIB_STATS_DISABLE;
	if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(VOS_MODULE_ID_WDA,
					&vos_msg))) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("Failed to post msg to WDA"));
		status = eHAL_STATUS_FAILURE;
	}
	return status;
}

/**
 * sme_get_mib_stats() - sme function to get mib stats
 * @hal_handle: reference to the HAL
 * @callback: callback handler
 * @context: mib stats context
 * @vos_context: vos context
 * @session_id: session id
 *
 * Return: hal_status
 */
eHalStatus sme_get_mib_stats(tHalHandle hal,
		csr_mib_stats_callback callback,
		void *context, void *vos_context,
		uint8_t session_id)
{
	eHalStatus status = eHAL_STATUS_FAILURE;
	tpAniSirGlobal mac = PMAC_STRUCT(hal);
	struct get_mib_stats_req *msg = NULL;
	vos_msg_t vos_message;

	msg = vos_mem_malloc(sizeof(struct get_mib_stats_req));
	status = sme_AcquireGlobalLock(&mac->sme);
	if (HAL_STATUS_SUCCESS(status)) {
		if (!msg) {
			smsLog(mac, LOGE, "%s: failed to allocate mem for req",
				__func__);
			return status;
		}

		msg->msg_type = WDA_MIB_STATS_REQ;
		msg->msg_len = (tANI_U16)sizeof(struct get_mib_stats_req);
		msg->session_id = session_id;
		mac->sme.mib_stats_context = context;
		mac->sme.csr_mib_stats_callback = callback;

		vos_message.type = WDA_MIB_STATS_REQ;
		vos_message.bodyptr = msg;
		vos_message.reserved = 0;

		if (!VOS_IS_STATUS_SUCCESS(vos_mq_post_message(
					VOS_MODULE_ID_WDA, &vos_message))) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
					"%s: Get Mib Stats Request fail",
					__func__);
			vos_mem_free(msg);
			mac->sme.mib_stats_context = NULL;
			mac->sme.csr_mib_stats_callback = NULL;
			status = eHAL_STATUS_FAILURE;
		}

		sme_ReleaseGlobalLock(&mac->sme);
	} else {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				FL("sme_AcquireGlobalLock failed"));
		vos_mem_free(msg);
	}
	return status;
}

/**
 * sme_update_fine_time_measurement_capab() - Update the FTM capab from incoming
 * val
 * @hal:    Handle for Hal layer
 * @val:    New FTM capability value
 *
 * Return: None
 */
void sme_update_fine_time_measurement_capab(tHalHandle hal, uint32_t val)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal);
	mac_ctx->fine_time_meas_cap = val;
	if (val == 0) {
		mac_ctx->rrm.rrmPEContext.rrmEnabledCaps.fine_time_meas_rpt = 0;
		((tpRRMCaps)mac_ctx->rrm.rrmSmeContext.
			rrmConfig.rm_capability)->fine_time_meas_rpt = 0;
	} else {
		mac_ctx->rrm.rrmPEContext.rrmEnabledCaps.fine_time_meas_rpt = 1;
		((tpRRMCaps)mac_ctx->rrm.rrmSmeContext.
			rrmConfig.rm_capability)->fine_time_meas_rpt = 1;
	}
}

eHalStatus sme_update_txrate(tHalHandle hal,
			struct sir_txrate_update *req)
{
	eHalStatus          status     = eHAL_STATUS_SUCCESS;
	VOS_STATUS          vos_status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal      mac_ctx    = PMAC_STRUCT(hal);
	vos_msg_t           vos_msg;
	struct sir_txrate_update *txrate_update;

	smsLog(mac_ctx, LOG1, FL("enter"));

	txrate_update = vos_mem_malloc(sizeof(*txrate_update));
	if (NULL == txrate_update) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("Failed to alloc txrate_update"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	txrate_update->session_id = req->session_id;
	txrate_update->txrate = req->txrate;
	vos_mem_copy(txrate_update->bssid, req->bssid, VOS_MAC_ADDR_SIZE);

	status = sme_AcquireGlobalLock(&mac_ctx->sme);
	if (eHAL_STATUS_SUCCESS == status) {
		/* Serialize the req through MC thread */
		vos_msg.bodyptr = txrate_update;
		vos_msg.type = WDA_UPDATE_TX_RATE;
		vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &vos_msg);

		if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
			VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				FL("Post Update tx_rate msg fail"));
			status = eHAL_STATUS_FAILURE;
			vos_mem_free(txrate_update);
		}
		sme_ReleaseGlobalLock(&mac_ctx->sme);
	} else {

		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
		FL("sme_AcquireGlobalLock failed"));
		vos_mem_free(txrate_update);
	}
	smsLog(mac_ctx, LOG1, FL("exit"));
	return status;
}

/**
 * sme_delete_all_tdls_peers: send request to delete tdls peers
 * @hal: handler for HAL
 * @sessionId: session id
 *
 * Functtion send's request to lim to delete tdls peers
 *
 * Return: Success: eHAL_STATUS_SUCCESS Failure: Error value
 */
eHalStatus sme_delete_all_tdls_peers(tHalHandle hal, uint8_t session_id)
{
	struct sir_del_all_tdls_peers *msg;
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal p_mac = PMAC_STRUCT(hal);
	tCsrRoamSession *session = CSR_GET_SESSION(p_mac, session_id);

	msg = vos_mem_malloc(sizeof(*msg));
	if (NULL == msg) {
		smsLog(p_mac, LOGE, FL("memory alloc failed"));
		return eHAL_STATUS_FAILURE;
	}

	vos_mem_set(msg, sizeof(*msg), 0);

	msg->msg_type = pal_cpu_to_be16((uint16_t)eWNI_SME_DEL_ALL_TDLS_PEERS);
	msg->msg_len =  pal_cpu_to_be16((uint16_t)sizeof(*msg));

	vos_mem_copy(msg->bssid, session->connectedProfile.bssid,
			sizeof(tSirMacAddr));

	status = palSendMBMessage(p_mac->hHdd, msg);

	if(status != eHAL_STATUS_SUCCESS) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("palSendMBMessage Failed"));
		status = eHAL_STATUS_FAILURE;
	}

	return status;
}


/**
 * sme_set_beacon_filter() - set the beacon filter configuration
 * @vdev_id: vdev index id
 * @ie_map: bitwise array of IEs
 *
 * Return: Return VOS_STATUS, otherwise appropriate failure code
 */
VOS_STATUS sme_set_beacon_filter(uint32_t vdev_id, uint32_t *ie_map)
{
	vos_msg_t vos_message;
	VOS_STATUS vos_status;
	struct beacon_filter_param *filter_param;

	filter_param = vos_mem_malloc(sizeof(*filter_param));
	if (NULL == filter_param) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: fail to alloc filter_param", __func__);
		return VOS_STATUS_E_FAILURE;
	}

	filter_param->vdev_id = vdev_id;

	vos_mem_copy(filter_param->ie_map, ie_map,
			BCN_FLT_MAX_ELEMS_IE_LIST*sizeof(uint32_t));

	vos_message.type = WDA_ADD_BCN_FILTER_CMDID;
	vos_message.bodyptr = filter_param;
	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA,
					&vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			"%s: Not able to post msg to WDA!",
			__func__);
		vos_mem_free(filter_param);
		return VOS_STATUS_E_FAILURE;
	}

	return vos_status;
}

/**
 * sme_set_btc_bt_wlan_interval_page_p2p() - Set the btc bt/p2p interval
 * @bt_interval: BT Page Interval
 * @bt_interval: P2P Interval
 *
 * Return: Return VOS_STATUS.
 */
VOS_STATUS sme_set_btc_bt_wlan_interval_page_p2p(uint32_t bt_interval,
			uint32_t p2p_interval)
{
	vos_msg_t msg = {0};
	VOS_STATUS vos_status;
	WMI_COEX_CONFIG_CMD_fixed_param *sme_interval;

	sme_interval = vos_mem_malloc(sizeof(*sme_interval));
	if (!sme_interval) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Malloc failed"));
		return VOS_STATUS_E_NOMEM;
	}

	sme_interval->config_type = WMI_COEX_CONFIG_PAGE_P2P_TDM;
	sme_interval->config_arg1 = bt_interval;
	sme_interval->config_arg2 = p2p_interval;

	msg.type = WDA_BTC_BT_WLAN_INTERVAL_CMD;
	msg.reserved = 0;
	msg.bodyptr = sme_interval;

	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA,&msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to post message to WDA"));
		vos_mem_free(sme_interval);
		return VOS_STATUS_E_FAILURE;
	}

	return vos_status;
}

/**

 * sme_unset_beacon_filter() - set the beacon filter configuration
 * @vdev_id: vdev index id
 *
 * Return: Return VOS_STATUS, otherwise appropriate failure code
 */
VOS_STATUS sme_unset_beacon_filter(uint32_t vdev_id)
{
	vos_msg_t vos_message;
	VOS_STATUS vos_status;
	struct beacon_filter_param *filter_param;

	filter_param = vos_mem_malloc(sizeof(*filter_param));
	if (NULL == filter_param) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
				"%s: fail to alloc filter_param", __func__);
		return VOS_STATUS_E_FAILURE;
	}

	filter_param->vdev_id = vdev_id;

	vos_message.type = WDA_REMOVE_BCN_FILTER_CMDID;
	vos_message.bodyptr = filter_param;
	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA,
					&vos_message);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			"%s: Not able to post msg to WDA!",
			__func__);
		vos_mem_free(filter_param);
		return VOS_STATUS_E_FAILURE;
	}

	return vos_status;
}

/**
 * sme_set_btc_bt_wlan_interval_page_sta() - Set the btc bt/sta interval
 * @bt_interval: BT Page Interval
 * @sta_interval: STA Interval
 *
 * Return: Return VOS_STATUS.
 */
VOS_STATUS sme_set_btc_bt_wlan_interval_page_sta(uint32_t bt_interval,
			uint32_t sta_interval)
{
	vos_msg_t msg = {0};
	VOS_STATUS vos_status;
	WMI_COEX_CONFIG_CMD_fixed_param *sme_interval;

	sme_interval = vos_mem_malloc(sizeof(*sme_interval));
	if (!sme_interval) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Malloc failed"));
		return VOS_STATUS_E_NOMEM;
	}

	sme_interval->config_type = WMI_COEX_CONFIG_PAGE_STA_TDM;
	sme_interval->config_arg1 = bt_interval;
	sme_interval->config_arg2 = sta_interval;

	msg.type = WDA_BTC_BT_WLAN_INTERVAL_CMD;
	msg.reserved = 0;
	msg.bodyptr = sme_interval;

	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA,&msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to post message to WDA"));
		vos_mem_free(sme_interval);
		return VOS_STATUS_E_FAILURE;
	}

	return vos_status;
}

/**
 * sme_set_btc_bt_wlan_interval_page_sap() - Set the btc bt/sap interval
 * @bt_interval: BT Page Interval
 * @bt_interval: SAP Interval
 *
 * Return: Return VOS_STATUS.
 */
VOS_STATUS sme_set_btc_bt_wlan_interval_page_sap(uint32_t bt_interval,
			uint32_t sap_interval)
{
	vos_msg_t msg = {0};
	VOS_STATUS vos_status;
	WMI_COEX_CONFIG_CMD_fixed_param *sme_interval;

	sme_interval = vos_mem_malloc(sizeof(*sme_interval));
	if (!sme_interval) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Malloc failed"));
		return VOS_STATUS_E_NOMEM;
	}

	sme_interval->config_type = WMI_COEX_CONFIG_PAGE_SAP_TDM;
	sme_interval->config_arg1 = bt_interval;
	sme_interval->config_arg2 = sap_interval;

	msg.type = WDA_BTC_BT_WLAN_INTERVAL_CMD;
	msg.reserved = 0;
	msg.bodyptr = sme_interval;

	vos_status = vos_mq_post_message(VOS_MODULE_ID_WDA,&msg);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("Not able to post message to WDA"));
		vos_mem_free(sme_interval);
		return VOS_STATUS_E_FAILURE;
	}

	return vos_status;
}

/**
 * sme_send_disassoc_req_frame - send disassoc req
 * @hal: handler to hal
 * @session_id: session id
 * @peer_mac: peer mac address
 * @reason: reason for disassociation
 * wait_for_ack: wait for acknowledgment
 *
 * function to send disassoc request to lim
 *
 * return: none
 */
void sme_send_disassoc_req_frame(tHalHandle hal, uint8_t session_id,
	uint8_t *peer_mac, uint16_t reason, uint8_t wait_for_ack)
{
	struct sme_send_disassoc_frm_req *msg;
	eHalStatus status = eHAL_STATUS_SUCCESS;
	tpAniSirGlobal p_mac = PMAC_STRUCT(hal);
	tANI_U8 *buf;
	tANI_U16 tmp;

	msg = vos_mem_malloc(sizeof(struct sme_send_disassoc_frm_req));

	if (NULL == msg)
		status = eHAL_STATUS_FAILURE;
	else
		status = eHAL_STATUS_SUCCESS;
	if (!HAL_STATUS_SUCCESS(status))
		return;

	vos_mem_set(msg, sizeof(struct sme_send_disassoc_frm_req), 0);
	msg->msg_type = pal_cpu_to_be16((tANI_U16)eWNI_SME_SEND_DISASSOC_FRAME);

	msg->length =
	    pal_cpu_to_be16((tANI_U16)sizeof(struct sme_send_disassoc_frm_req));

	buf = &msg->session_id;

	/* session id */
	*buf = (tANI_U8) session_id;
	buf += sizeof(tANI_U8);

	/* transaction id */
	*buf = 0;
	*(buf + 1) = 0;
	buf += sizeof(tANI_U16);

	/* Set the peer MAC address before sending the message to LIM */
	vos_mem_copy(buf, peer_mac, VOS_MAC_ADDR_SIZE);

	buf += VOS_MAC_ADDR_SIZE;

	/* reasoncode */
	tmp = pal_cpu_to_be16(reason);
	vos_mem_copy(buf, &tmp, sizeof(tANI_U16));
	buf += sizeof(tANI_U16);

	*buf =  wait_for_ack;
	buf += sizeof(tANI_U8);

	status = palSendMBMessage(p_mac->hHdd, msg );

	if(status != eHAL_STATUS_SUCCESS)
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			FL("palSendMBMessage Failed"));
}

/*
 *  sme_is_session_valid(): verify a sme session
 *  @param hal_handle: hal handle for getting global mac struct.
 *  @param session_id: sme_session_id
 *  Return: eHAL_STATUS_SUCCESS or non-zero on failure.
 */
VOS_STATUS sme_is_session_valid(tHalHandle hal_handle, uint8_t session_id)
{
	tpAniSirGlobal mac_ctx = PMAC_STRUCT(hal_handle);

	if (NULL == mac_ctx) {
		VOS_TRACE(VOS_MODULE_ID_SME, VOS_TRACE_LEVEL_ERROR,
			  FL("mac_ctx is null!!"));
	        VOS_ASSERT(0);
	        return VOS_STATUS_E_FAILURE;
	}
	if (CSR_IS_SESSION_VALID(mac_ctx, session_id))
	        return VOS_STATUS_SUCCESS;

	return VOS_STATUS_E_FAILURE;
}

/**
 * sme_enable_disable_chanavoidind_event - configure ca event ind
 * @hal: handler to hal
 * set_val: enable/disable
 *
 * function to enable/disable chan avoidance indication
 *
 * return: eHalStatus
 */
eHalStatus sme_enable_disable_chanavoidind_event(tHalHandle hal,
                                              tANI_U8 set_value)
{
	eHalStatus status = eHAL_STATUS_SUCCESS;
	VOS_STATUS vos_status;
	tpAniSirGlobal mac = PMAC_STRUCT(hal);
	vos_msg_t msg;

	smsLog(mac, LOG1, FL("set_value: %d"), set_value);
	if (eHAL_STATUS_SUCCESS ==  sme_AcquireGlobalLock(&mac->sme)) {
		vos_mem_zero(&msg, sizeof(vos_msg_t));
		msg.type = WDA_SEND_FREQ_RANGE_CONTROL_IND;
		msg.reserved = 0;
		msg.bodyval = set_value;
		vos_status = vos_mq_post_message(VOS_MQ_ID_WDA, &msg);
		if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
			status = eHAL_STATUS_FAILURE;
		}
		sme_ReleaseGlobalLock(&mac->sme);
		return status;
	}

	return eHAL_STATUS_FAILURE;
}
/**
 * sme_oem_update_capability() - update UMAC's oem related capability.
 * @hal: Handle returned by mac_open
 * @oem_cap: pointer to oem_capability
 *
 * This function updates OEM capability to UMAC. Currently RTT
 * related capabilities are updated. More capabilities can be
 * added in future.
 *
 * Return: VOS_STATUS
 */
VOS_STATUS sme_oem_update_capability(tHalHandle hal,
				     struct sme_oem_capability *cap)
{
	VOS_STATUS status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal pmac = PMAC_STRUCT(hal);
	uint8_t *bytes;

	bytes = pmac->rrm.rrmSmeContext.rrmConfig.rm_capability;

	if (cap->ftm_rr)
		bytes[4] |= RM_CAP_FTM_RANGE_REPORT;
	if (cap->lci_capability)
		bytes[4] |= RM_CAP_CIVIC_LOC_MEASUREMENT;

	return status;
}

/**
 * sme_oem_get_capability() - get oem capability
 * @hal: Handle returned by mac_open
 * @oem_cap: pointer to oem_capability
 *
 * This function is used to get the OEM capability from UMAC.
 * Currently RTT related capabilities are received. More
 * capabilities can be added in future.
 *
 * Return: VOS_STATUS
 */
VOS_STATUS sme_oem_get_capability(tHalHandle hal,
				  struct sme_oem_capability *cap)
{
	VOS_STATUS status = VOS_STATUS_SUCCESS;
	tpAniSirGlobal pmac = PMAC_STRUCT(hal);
	uint8_t *bytes;

	bytes = pmac->rrm.rrmSmeContext.rrmConfig.rm_capability;

	cap->ftm_rr = bytes[4] & RM_CAP_FTM_RANGE_REPORT;
	cap->lci_capability = bytes[4] & RM_CAP_CIVIC_LOC_MEASUREMENT;

	return status;
}
