/*************************************************************************\
* Copyright (c) 2016 Helmholtz-Zentrum Berlin
*     fuer Materialien und Energie GmbH (HZB), Berlin, Germany.
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU Lesser General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
\*************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

// #EPICS LIBS
#include <dbAccess.h>
#include <dbEvent.h>
#include <dbScan.h>
#include <dbStaticLib.h>
#include <epicsExport.h>
#include <initHooks.h>
#include <devSup.h>
#include <recSup.h>
#include <recGbl.h>

#include <aiRecord.h>
#include <aaiRecord.h>
#include <aoRecord.h>
#include <aaoRecord.h>
#include <biRecord.h>
#include <boRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <stringinRecord.h>
#include <stringoutRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <mbbiDirectRecord.h>
#include <mbboDirectRecord.h>
#include <asTrapWrite.h>
#include <alarm.h>
#include <asDbLib.h>
#include <cvtTable.h>
#include <menuFtype.h>
#include <menuAlarmSevr.h>
#include <menuAlarmStat.h>
#include <menuConvert.h>
//#define GEN_SIZE_OFFSET
#include <waveformRecord.h>
//#undef  GEN_SIZE_OFFSET

#include "drvOpcUa.h"
#include "devUaClient.h"

using namespace UaClientSdk;

#ifdef _WIN32
__inline int debug_level(dbCommon *prec) {
#else
inline int debug_level(dbCommon *prec) {
#endif
        return prec->tpro;
}

#ifdef _WIN32
#define strncasecmp _strnicmp
#endif

#define DEBUG_LEVEL debug_level((dbCommon*)prec)

static  long         read(dbCommon *prec);
static  long         write(dbCommon *prec,UaVariant &var);
static  void         outRecordCallback(CALLBACK *pcallback);
static  long         get_ioint_info(int cmd, dbCommon *prec, IOSCANPVT * ppvt);

//extern int OpcUaInitItem(char *OpcUaName, dbCommon* pRecord, OPCUA_ItemINFO** uaItem);
//extern void checkOpcUaVariables(void);

// Configurable defaults for sampling interval, queue size, discard policy

static double drvOpcua_DefaultSamplingInterval = -1.0;  // ms (-1 = use publishing interval)
static int drvOpcua_DefaultQueueSize = 1;               // no queueing
static int drvOpcua_DefaultDiscardOldest = 1;           // discard oldest value in case of overrun

epicsExportAddress(double, drvOpcua_DefaultSamplingInterval);
epicsExportAddress(int, drvOpcua_DefaultQueueSize);
epicsExportAddress(int, drvOpcua_DefaultDiscardOldest);

/*+**************************************************************************
 *		DSET functions
 **************************************************************************-*/
extern "C" {
long init (int after);

static long init_longout  (struct longoutRecord* plongout);
static long write_longout (struct longoutRecord* plongout);

static long init_longin (struct longinRecord* pmbbid);
static long read_longin (struct longinRecord* pmbbid);
static long init_mbbiDirect (struct mbbiDirectRecord* pmbbid);
static long read_mbbiDirect (struct mbbiDirectRecord* pmbbid);
static long init_mbboDirect (struct mbboDirectRecord* pmbbid);
static long write_mbboDirect (struct mbboDirectRecord* pmbbid);
static long init_mbbi (struct mbbiRecord* pmbbid);
static long read_mbbi (struct mbbiRecord* pmbbid);
static long init_mbbo (struct mbboRecord* pmbbod);
static long write_mbbo (struct mbboRecord* pmbbod);
static long init_bi  (struct biRecord* pbi);
static long read_bi (struct biRecord* pbi);
static long init_bo  (struct boRecord* pbo);
static long write_bo (struct boRecord* pbo);
static long init_ai (struct aiRecord* pai);
static long read_ai (struct aiRecord* pai);
static long init_ao  (struct aoRecord* pao);
static long write_ao (struct aoRecord* pao);
static long init_stringin (struct stringinRecord* pstringin);
static long read_stringin (struct stringinRecord* pstringin);
static long init_stringout  (struct stringoutRecord* pstringout);
static long write_stringout (struct stringoutRecord* pstringout);

typedef struct {
    long number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN write_record;
} OpcUaDSET;

OpcUaDSET devlongoutOpcUa =    {5, NULL, (DEVSUPFUN)init, (DEVSUPFUN)init_longout, (DEVSUPFUN)get_ioint_info,(DEVSUPFUN) write_longout  };
epicsExportAddress(dset,devlongoutOpcUa);

OpcUaDSET devlonginOpcUa =     {5, NULL,(DEVSUPFUN) init, (DEVSUPFUN)init_longin, (DEVSUPFUN)get_ioint_info, (DEVSUPFUN)read_longin	 };
epicsExportAddress(dset,devlonginOpcUa);

OpcUaDSET devmbbiDirectOpcUa = {5, NULL, (DEVSUPFUN)init,(DEVSUPFUN) init_mbbiDirect,(DEVSUPFUN) get_ioint_info, (DEVSUPFUN)read_mbbiDirect};
epicsExportAddress(dset,devmbbiDirectOpcUa);

OpcUaDSET devmbboDirectOpcUa = {5, NULL,(DEVSUPFUN) init,(DEVSUPFUN) init_mbboDirect,(DEVSUPFUN) get_ioint_info, (DEVSUPFUN)write_mbboDirect};
epicsExportAddress(dset,devmbboDirectOpcUa);

OpcUaDSET devmbbiOpcUa = {5, NULL,(DEVSUPFUN) init, (DEVSUPFUN)init_mbbi,(DEVSUPFUN) get_ioint_info, (DEVSUPFUN)read_mbbi};
epicsExportAddress(dset,devmbbiOpcUa);

OpcUaDSET devmbboOpcUa = {5, NULL, (DEVSUPFUN)init, (DEVSUPFUN)init_mbbo, (DEVSUPFUN)get_ioint_info, (DEVSUPFUN)write_mbbo};
epicsExportAddress(dset,devmbboOpcUa);

OpcUaDSET devbiOpcUa = {5, NULL, (DEVSUPFUN)init, (DEVSUPFUN)init_bi,(DEVSUPFUN) get_ioint_info, (DEVSUPFUN)read_bi};
epicsExportAddress(dset,devbiOpcUa);

OpcUaDSET devboOpcUa = {5, NULL, (DEVSUPFUN)init, (DEVSUPFUN)init_bo, (DEVSUPFUN)get_ioint_info, (DEVSUPFUN)write_bo};
epicsExportAddress(dset,devboOpcUa);

OpcUaDSET devstringinOpcUa = {5, NULL, (DEVSUPFUN)init, (DEVSUPFUN)init_stringin, (DEVSUPFUN)get_ioint_info, (DEVSUPFUN)read_stringin};
epicsExportAddress(dset,devstringinOpcUa);

OpcUaDSET devstringoutOpcUa = {5, NULL,(DEVSUPFUN) init, (DEVSUPFUN)init_stringout, (DEVSUPFUN)get_ioint_info,(DEVSUPFUN) write_stringout};
epicsExportAddress(dset,devstringoutOpcUa);

struct aidset { // analog input dset
    long		number;
    DEVSUPFUN	dev_report;
    DEVSUPFUN	init;
    DEVSUPFUN	init_record; 	    //returns: (-1,0)=>(failure,success)
    DEVSUPFUN	get_ioint_info;
    DEVSUPFUN	read_ai;    	    // 2 => success, don't convert)
    // if convert then raw value stored in rval
    DEVSUPFUN	special_linconv;
} devaiOpcUa =         {6, NULL, (DEVSUPFUN)init, (DEVSUPFUN)init_ai, (DEVSUPFUN)get_ioint_info, (DEVSUPFUN)read_ai, NULL };
epicsExportAddress(dset,devaiOpcUa);

struct aodset { // analog input dset
    long		number;
    DEVSUPFUN	dev_report;
    DEVSUPFUN	init;
    DEVSUPFUN	init_record; 	    //returns: 2=> success, no convert
    DEVSUPFUN	get_ioint_info;
    DEVSUPFUN	write_ao;   	    //(0)=>(success )
    DEVSUPFUN	special_linconv;
} devaoOpcUa =         {6, NULL, (DEVSUPFUN)init, (DEVSUPFUN)init_ao,(DEVSUPFUN) get_ioint_info,(DEVSUPFUN) write_ao, NULL };
epicsExportAddress(dset,devaoOpcUa);

static long init_waveformRecord(struct waveformRecord* prec);
static long read_wf(struct waveformRecord *prec);
struct {
    long number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_Record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read;
    DEVSUPFUN special_linconv;
} devwaveformOpcUa = {
    6,
    NULL,
    NULL,
    (DEVSUPFUN)init_waveformRecord,
    (DEVSUPFUN)get_ioint_info,
    (DEVSUPFUN)read_wf,
    NULL
};
epicsExportAddress(dset,devwaveformOpcUa);

} // extern C

/***************************************************************************
 *		Defines and Locals
 **************************************************************************-*/

/***************************************************************************
 *      Scan info items for option settings
 ***************************************************************************/

static void scanInfoItems(const dbCommon *pcommon, OPCUA_ItemINFO *uaItem)
{
    long status;
    DBENTRY dbentry;
    DBENTRY *pdbentry = &dbentry;

    dbInitEntry(pdbbase, pdbentry);

    status = dbFindRecord(pdbentry, pcommon->name);
    if (status) {
        dbFinishEntry(pdbentry);
        return;
    }

    if (dbFindInfo(pdbentry, "opcua:RDBKOFF") == 0) {
        uaItem->flagRdbkOff = 1;
    }
    if (dbFindInfo(pdbentry, "opcua:SAMPLING") == 0) {
        uaItem->samplingInterval = atof(dbGetInfoString(pdbentry));
    }
    if (dbFindInfo(pdbentry, "opcua:QSIZE") == 0) {
        uaItem->queueSize = (epicsUInt32) atoi(dbGetInfoString(pdbentry));
    }
    if (dbFindInfo(pdbentry, "opcua:DISCARD") == 0) {
        if (strncasecmp(dbGetInfoString(pdbentry), "new", 3) == 0) {
            uaItem->discardOldest = 0;
        }
    }
    dbFinishEntry(pdbentry);
}

static void opcuaMonitorControl (initHookState state)
{
    switch (state) {
    case initHookAtIocRun:
        OpcUaSetupMonitors();
        break;
    default:
        break;
    }
}

long init (int after)
{
    static int done = 0;

    if (!done) {
        done = 1;
        return (initHookRegister(opcuaMonitorControl));
    }
    return 0;
}

long init_common (dbCommon *prec, struct link* plnk, epicsType recType, int inpType)
{
    OPCUA_ItemINFO* uaItem;

    if(plnk->type != INST_IO) {
        long status;
        if (inpType) status = S_dev_badOutType; else status = S_dev_badInpType;
        recGblRecordError(status, prec, "devOpcUa (init_record) Bad INP/OUT link type (must be INST_IO)");
        return status;
    }

    uaItem =  (OPCUA_ItemINFO *) calloc(1,sizeof(OPCUA_ItemINFO));
    if (!uaItem) {
        long status = S_db_noMemory;
        recGblRecordError(status, prec, "devOpcUa (init_record) Out of memory, calloc() failed");
        return status;
    }

    if(strlen(plnk->value.instio.string) < ITEMPATHLEN) {
        strcpy(uaItem->ItemPath,plnk->value.instio.string);
        if(addOPCUA_Item(uaItem)) {
            recGblRecordError(S_dev_NoInit, prec, "drvOpcUa not initialized");
        }
    }
    else {
        long status = S_db_badField;
        recGblRecordError(status, prec, "devOpcUa (init_record) INP/OUT field too long");
        return status;
    }

    prec->dpvt = uaItem;
    uaItem->recDataType = recType;
    uaItem->stat = 1;       // not conntcted
    uaItem->flagRdbkOff = 0;
    uaItem->isArray = 0;    // default, set in init_record()
    uaItem->prec = prec;
    uaItem->debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1
    uaItem->flagLock = epicsMutexMustCreate();
    uaItem->samplingInterval = drvOpcua_DefaultSamplingInterval;
    uaItem->queueSize = drvOpcua_DefaultQueueSize;
    uaItem->discardOldest = drvOpcua_DefaultDiscardOldest;
    scanInfoItems(prec, uaItem);
    if(uaItem->debug >= 2)
        errlogPrintf("init_common %s\t PACT= %i\n", prec->name, prec->pact);
    // get OPC item type in init -> after

    if(inpType) { // is OUT record
        uaItem->inpDataType = (epicsType) inpType;

        callbackSetCallback(outRecordCallback, &(uaItem->callback));
        callbackSetUser(prec, &(uaItem->callback));
    }
    else {
        scanIoInit(&(uaItem->ioscanpvt));
    }
    return 0;
}

template<typename T>
long toOpcuaTypeVariant(OPCUA_ItemINFO* uaItem,UaVariant &var,T VAL)
{
    if(!uaItem) {
        if(uaItem->debug > 0) errlogPrintf("%s illegal *uaItem\n", uaItem->prec->name);
        return -1;
    }
    try {
        switch((int)uaItem->itemDataType){
        case OpcUaType_Boolean: var.setBool( (0 != VAL)?true:false); break;
        case OpcUaType_SByte:   var.setByte(VAL);break;
        case OpcUaType_Byte:    var.setByte(VAL);break;
        case OpcUaType_Int16:   var.setInt16(VAL);break;
        case OpcUaType_UInt16:  var.setUInt16(VAL);break;
        case OpcUaType_Int32:   var.setInt32( VAL);break;
        case OpcUaType_Float:   var.setFloat(VAL);break;
        case OpcUaType_Double:  var.setDouble(VAL);break;
      //case OpcUaType_String: //won't work: var.SetString needs UaString as argument, but there is no UaString constructor for numbers!
        default:
            return 1;
        }
    }
    catch(...) {
        errlogPrintf("%s: Exception in toOpcuaTypeVariant() val=%f %s",uaItem->prec->name,(double)VAL,variantTypeStrings(uaItem->itemDataType));
    }

    return 0;
}


/***************************************************************************
                                Longin Support
 **************************************************************************-*/
long init_longin (struct longinRecord* prec)
{
    return init_common((dbCommon*)prec,&(prec->inp),epicsInt32T,0);
}

long read_longin (struct longinRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    epicsMutexLock(uaItem->flagLock);
    long ret = read((dbCommon*)prec);;

    if (!ret) {
        if(uaItem->varVal.toInt32(prec->val)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toInt32 OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else
            prec->udf = FALSE;
        if(DEBUG_LEVEL >= 2) errlogPrintf("longin      %s %s\tVAL:%d\n",getTime(buf),prec->name,prec->val);
    }
    epicsMutexUnlock(uaItem->flagLock);
    return ret;
}

/***************************************************************************
                                Longout Support
 ***************************************************************************/
long init_longout( struct longoutRecord* prec)
{
    return init_common((dbCommon*)prec,&(prec->out),epicsInt32T,epicsInt32T);
}

long write_longout (struct longoutRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret=0;
    UaVariant var;

    if(uaItem->prec->tpro > 1)
        uaItem->debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1
    if (uaItem->flagIsRdbk ) {
        if(uaItem->varVal.toInt32(prec->val)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toInt32 OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else
            prec->udf = FALSE;
    }
    else {
        ret = toOpcuaTypeVariant(uaItem,var,prec->val);
        if( !ret)
            ret = write((dbCommon*)prec,var);

    }
    if(DEBUG_LEVEL >= 2) errlogPrintf("longout     %s %s\tVAL:%d\n",getTime(buf),prec->name,prec->val);
    if(ret)
        recGblSetSevr(prec,menuAlarmStatWRITE,menuAlarmSevrINVALID);
    return ret;
}

/*+**************************************************************************
                                MbbiDirect Support
 **************************************************************************-*/
long init_mbbiDirect (struct mbbiDirectRecord* prec)
{
    prec->mask <<= prec->shft;
    return init_common((dbCommon*)prec,&(prec->inp),epicsUInt32T,0);
}

long read_mbbiDirect (struct mbbiDirectRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret = read((dbCommon*)prec);

    epicsMutexLock(uaItem->flagLock);
    if (!ret) {
        epicsUInt32 rval;
        if(uaItem->varVal.toUInt32(rval)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toUInt32 OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else {
            prec->udf = FALSE;
            prec->rval = rval & prec->mask;
        }
        if(DEBUG_LEVEL >= 2) errlogPrintf("mbbiDirect  %s %s\tVAL:%d RVAL:%d\n",getTime(buf),prec->name,prec->val,prec->rval);
    }
    epicsMutexUnlock(uaItem->flagLock);
    return ret;
}

/***************************************************************************
                                mbboDirect Support
 ***************************************************************************/
long init_mbboDirect( struct mbboDirectRecord* prec)
{
    prec->mask <<= prec->shft;
    return init_common((dbCommon*)prec,&(prec->out),epicsUInt32T,epicsUInt32T);
}

long write_mbboDirect (struct mbboDirectRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret=0;
    UaVariant var;

    if(uaItem->prec->tpro > 1)
        uaItem->debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1

    if (uaItem->flagIsRdbk) {

        epicsUInt32 rval;
        if(uaItem->varVal.toUInt32(rval)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toUInt32 OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else {
            prec->rval = rval = rval & prec->mask;
            if (prec->shft > 0)
                rval >>= prec->shft;
            prec->val = rval;
            prec->udf = FALSE;
        }
    }
    else {
        ret = toOpcuaTypeVariant(uaItem,var,(prec->rval & prec->mask));
        if( !ret)
            ret = write((dbCommon*)prec,var);
    }
    if(DEBUG_LEVEL >= 2) errlogPrintf("mbboDirect  %s %s\tRVAL:%d\n",getTime(buf),prec->name,prec->rval);
    if(ret) {
        recGblSetSevr(prec,menuAlarmStatWRITE,menuAlarmSevrINVALID);
    }
    return ret;
}

/*+**************************************************************************
                                Mbbi Support
 **************************************************************************-*/
long init_mbbi (struct mbbiRecord* prec)
{
    prec->mask <<= prec->shft;
    return init_common((dbCommon*)prec,&(prec->inp),epicsUInt32T,0);
}

long read_mbbi (struct mbbiRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret;

    epicsMutexLock(uaItem->flagLock);
    ret = read((dbCommon*)prec);
    if (!ret) {
        epicsUInt32 rval;
        if(uaItem->varVal.toUInt32(rval)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toUInt32 OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else
        {
            prec->rval = rval & prec->mask;
            prec->udf = FALSE;
        }
        if(DEBUG_LEVEL >= 2) errlogPrintf("mbbi          %s %s\tVAL:%d RVAL:%d\n",getTime(buf),prec->name,prec->val,prec->rval);
    }
    epicsMutexUnlock(uaItem->flagLock);
    return ret;
}

/***************************************************************************
                                mbbo Support
 ***************************************************************************/
long init_mbbo( struct mbboRecord* prec)
{
    prec->mask <<= prec->shft;
    return init_common((dbCommon*)prec,&(prec->out),epicsUInt32T,epicsUInt32T);
}

long write_mbbo (struct mbboRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret=0;
    UaVariant var;

    if(uaItem->prec->tpro > 1)
        uaItem->debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1
    if (uaItem->flagIsRdbk) {
        epicsUInt32 rval;
        if(uaItem->varVal.toUInt32(rval)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toUInt32 OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else {
            rval = prec->rval = rval & prec->mask;
            if (prec->shft > 0)
                rval >>= prec->shft;
            if (prec->sdef) {
                epicsUInt32 *pstate_values = &prec->zrvl;
                int i;
                prec->val = 65535;        /* initalize to unknown state */
                for (i = 0; i < 16; i++) {
                    if (*pstate_values == rval) {
                        prec->val = i;
                        break;
                    }
                    pstate_values++;
                }
            }
            else {
                /* No defined states, punt */
                prec->val = rval;
            }
            prec->udf = FALSE;
        }
    }
    else {
        errlogPrintf("write_mbbo RVAL=%X\n",prec->rval);
        ret = toOpcuaTypeVariant(uaItem,var,(prec->rval & prec->mask));
        if( !ret)
            ret = write((dbCommon*)prec,var);
    }
    if(DEBUG_LEVEL >= 2) errlogPrintf("mbbo        %s %s\tRVAL:%d\n",getTime(buf),prec->name,prec->rval);
    if(ret) {
        recGblSetSevr(prec,menuAlarmStatWRITE,menuAlarmSevrINVALID);
    }
    return ret;
}

/*+**************************************************************************
                                Bi Support
 **************************************************************************-*/
long init_bi (struct biRecord* prec)
{
    return init_common((dbCommon*)prec,&(prec->inp),epicsUInt32T,0);
}

long read_bi (struct biRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret = 0;

    epicsMutexLock(uaItem->flagLock);
    ret = read((dbCommon*)prec);
    if (!ret) {
        if(uaItem->varVal.toUInt32(prec->rval)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toUInt32 OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else {
            if(prec->rval==0) prec->val = 0;
            else prec->val = 1;
        }

        if(DEBUG_LEVEL >= 2) errlogPrintf("bi          %s %s\tRVAL:%d\n",getTime(buf),prec->name,prec->rval);
    }
    epicsMutexUnlock(uaItem->flagLock);
    return ret;
}


/***************************************************************************
                                bo Support
 ***************************************************************************/
long init_bo( struct boRecord* prec)
{
    prec->mask=1;
    return init_common((dbCommon*)prec,&(prec->out),epicsUInt32T,epicsUInt32T);
}

long write_bo (struct boRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret=0;
    UaVariant var;

    if(uaItem->prec->tpro > 1)
        uaItem->debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1
    if (uaItem->flagIsRdbk) {
        if(uaItem->varVal.toUInt32(prec->rval)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toUInt32 OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else {
            if(prec->rval==0) prec->val = 0;
            else prec->val = 1;
            prec->udf = FALSE;
        }
    }
    else {
        ret = toOpcuaTypeVariant(uaItem,var,(prec->rval & prec->mask));
        if( !ret)
            ret = write((dbCommon*)prec,var);
    }
    if(DEBUG_LEVEL >= 2) errlogPrintf("bo          %s %s\tRVAL:%d\n",getTime(buf),prec->name,prec->rval);
    if(ret) {
        recGblSetSevr(prec,menuAlarmStatWRITE,menuAlarmSevrINVALID);
    }
    return ret;
}

/*+**************************************************************************
                                ao Support
 **************************************************************************-*/
long init_ao (struct aoRecord* prec)
{
    long ret;
    if(prec->linr == menuConvertNO_CONVERSION)
        ret = init_common((dbCommon*)prec,&(prec->out),epicsFloat64T,epicsFloat64T);
    else
        ret = init_common((dbCommon*)prec,&(prec->out),epicsInt32T,epicsFloat64T);
    return ret;
}

long write_ao (struct aoRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret=0;
    UaVariant var;
    double value;

    if(uaItem->prec->tpro > 1)
        uaItem->debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1
    if (uaItem->flagIsRdbk) {
        if(uaItem->varVal.toDouble(value)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toDouble OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else {  // do conversion
            prec->rval = (epicsInt32) value;    // just to have a rval

            /* adjust slope and offset */
            if(prec->aslo!=0.0) value*=prec->aslo;
            value+=prec->aoff;

            switch (prec->linr ) {
            case menuConvertNO_CONVERSION:
                break;
            case menuConvertLINEAR:
            case menuConvertSLOPE:
                value = (value * prec->eslo) + prec->eoff;
                // ASLO/AOFF conversion
                if (prec->eslo != 0.0) value *= prec->eslo;
                value += prec->eoff;
                break;
            default: // must use breakpoint table
                if (cvtRawToEngBpt(&value,prec->linr,prec->init,(void **)&(prec->pbrk),&prec->lbrk)!=0) {
                    ret = 1;
                }
            }
            if (!ret) {
                prec->val = value;
                prec->udf = isnan(value);
            }
        }
    }
    else {
        value = prec->oval;

        // When OROC is set each process the record by dataChange callback is harmfull:
        // 1. inc- decrement will be done twice!
        // 2. The VAL field is set to the current OVAl field
        if(prec->omod!= 0)          // use omod to disable process by dataChange callback if oroc is set.
           uaItem->flagRdbkOff |= 2;   // set bit_1
        else
           uaItem->flagRdbkOff |= 2;   // set bit_1

        // Conversion as done in aoRecord->convert(), but keep type double to write out.
        // The record does the same conversion and sets rval (INT32).
        switch (prec->linr) {
        case menuConvertNO_CONVERSION:
            break; /* do nothing*/
        case menuConvertLINEAR:
        case menuConvertSLOPE:
            if (prec->eslo == 0.0) value = 0;
            else value = (value - prec->eoff) / prec->eslo;
            break;
        default:
            if (cvtEngToRawBpt(&value, prec->linr, prec->init,(void **)&(prec->pbrk), &prec->lbrk) != 0) {
                recGblSetSevr(prec, SOFT_ALARM, MAJOR_ALARM);
                return 1;
            }
        }

        value -= prec->aoff;
        if (prec->aslo != 0.0) value /= prec->aslo;

        ret = toOpcuaTypeVariant(uaItem,var,value);
        if( !ret)
            ret = write((dbCommon*)prec,var);
    }
    if(DEBUG_LEVEL >= 2) errlogPrintf("ao          %s %s\tOVAL %f OMOD:%d flagRdbkOff:%d ret:%ld\n",getTime(buf),prec->name,prec->oval, prec->omod,uaItem->flagRdbkOff,ret);
    if(ret) {
        recGblSetSevr(prec,menuAlarmStatWRITE,menuAlarmSevrINVALID);
    }
    return ret;
}
/***************************************************************************
                                ai Support
 ***************************************************************************/
long init_ai (struct aiRecord* prec)
{
    if(prec->linr == menuConvertNO_CONVERSION)
        return init_common((dbCommon*)prec,&(prec->inp),epicsFloat64T,0);
    else
        return init_common((dbCommon*)prec,&(prec->inp),epicsInt32T,0);
}

long read_ai (struct aiRecord* prec)
{
    char buf[256];
    long ret;   // Conversion done here!
    double value;
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*) prec->dpvt;

    epicsMutexLock(uaItem->flagLock);
    ret = read((dbCommon*)prec);
    if (!ret) {
        if(uaItem->varVal.toDouble(value)) {
            if(uaItem->debug) errlogPrintf("%s: conversion toDouble OutOfRange\n",uaItem->prec->name);
            ret = 1;
        }
        else {  // do conversion in the way the records convert() routine does it
            if(prec->linr == menuConvertNO_CONVERSION)  // just to have a rval
                prec->rval = (epicsInt32) value;

            /* adjust slope and offset */
            if(prec->aslo!=0.0) value*=prec->aslo;
            value+=prec->aoff;

            switch (prec->linr ) {
            case menuConvertNO_CONVERSION:
                break;
            case menuConvertLINEAR:
            case  menuConvertSLOPE:
                value = (value * prec->eslo) + prec->eoff;
                // ASLO/AOFF conversion
                if (prec->eslo != 0.0) value *= prec->eslo;
                value += prec->eoff;
                if(DEBUG_LEVEL>= 2) errlogPrintf("ai          %s %s\tbuf:%s VAL:%f\n", getTime(buf),(uaItem->varVal).toString().toUtf8(),prec->name,prec->val);
                break;
            default: // must use breakpoint table
                if (cvtRawToEngBpt(&value,prec->linr,prec->init,(void **)&(prec->pbrk),&prec->lbrk) != 0) {
                    recGblSetSevr(prec,SOFT_ALARM,MAJOR_ALARM);
                }
            }
            /* apply smoothing algorithm */
            if (prec->smoo != 0.0 && finite(prec->val)){
                if (prec->init) prec->val = value;	/* initial condition */
                prec->val = value * (1.00 - prec->smoo) + (prec->val * prec->smoo);
                prec->rval = (epicsInt32) prec->val;
            }else{
                prec->val = value;
                prec->rval = (epicsInt32) value;
            }
            prec->udf = isnan(prec->val);
        }
        if(DEBUG_LEVEL >= 2) errlogPrintf("ai          %s %s\tbuf:%f VAL: %f RVAL:%d\n", getTime(buf),prec->name,value,prec->val,prec->rval);
    }
    epicsMutexUnlock(uaItem->flagLock);
    return (ret==0)?2:ret;
}

/***************************************************************************
                                Stringin Support
 ***************************************************************************/
long init_stringin (struct stringinRecord* prec)
{
    return init_common((dbCommon*)prec,&(prec->inp),epicsOldStringT,0);
}

long read_stringin (struct stringinRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret = 0;

    epicsMutexLock(uaItem->flagLock);
    ret = read((dbCommon*)prec);
    if( !ret ) {
        strncpy(prec->val,uaItem->varVal.toString().toUtf8(),40);    // string length: see stringinRecord.h
        prec->udf = FALSE;	// stringinRecord process doesn't set udf field in case of no convert!
    }
    epicsMutexUnlock(uaItem->flagLock);
    if(DEBUG_LEVEL >= 2) errlogPrintf("stringin    %s %s\tVAL:%s\n",getTime(buf),prec->name,prec->val);
    return ret;
}

/***************************************************************************
                                Stringout Support
 ***************************************************************************/
long init_stringout( struct stringoutRecord* prec)
{
    return init_common((dbCommon*)prec,&(prec->out),epicsStringT,epicsStringT);
}

long write_stringout (struct stringoutRecord* prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    long ret=0;
    UaVariant var;


    if(uaItem->prec->tpro > 1)
        uaItem->debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1
    if( uaItem->flagIsRdbk) {
        if( uaItem->itemDataType != OpcUaType_String )
            ret = 1;
        else {
            strncpy(prec->val,uaItem->varVal.toString().toUtf8(),40);    // string length: see stringinRecord.h
            //FIXME: do not hardcode length - if no longer hardcoded in recordd
            prec->udf = FALSE;
        }
    }
    else {
        var.setString(prec->val);
        ret = write((dbCommon*)prec,var);
    }
    if(DEBUG_LEVEL >= 2) errlogPrintf("stringout   %s %s\tVAL:%s\n",getTime(buf),prec->name,prec->val);
    if(ret)
        recGblSetSevr(prec,menuAlarmStatWRITE,menuAlarmSevrINVALID);
    return ret;
}

/***************************************************************************
                                Waveform Support
 **************************************************************************-*/
long init_waveformRecord(struct waveformRecord* prec)
{
    long ret = 0;
    int recType=0;
    OPCUA_ItemINFO* uaItem=NULL;
    prec->dpvt = NULL;
    switch(prec->ftvl) {
        case menuFtypeSTRING: recType = epicsOldStringT; break;
        case menuFtypeCHAR  : recType = epicsInt8T; break;
        case menuFtypeUCHAR : recType = epicsUInt8T; break;
        case menuFtypeSHORT : recType = epicsInt16T; break;
        case menuFtypeUSHORT: recType = epicsUInt16T; break;
        case menuFtypeLONG  : recType = epicsInt32T; break;
        case menuFtypeULONG : recType = epicsUInt32T; break;
        case menuFtypeFLOAT : recType = epicsFloat32T; break;
        case menuFtypeDOUBLE: recType = epicsFloat64T; break;
        case menuFtypeENUM  : recType = epicsEnum16T; break;
    }
    ret = init_common((dbCommon*)prec,&(prec->inp),(epicsType) recType,0);
    uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    if(uaItem != NULL) {
        uaItem->isArray = 1;
        uaItem->arraySize = prec->nelm;
    }
    return  ret;
}

long read_wf(struct waveformRecord *prec)
{
    char buf[256];
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    epicsMutexLock(uaItem->flagLock);
    long ret = read((dbCommon*)prec);
    if(ret)
        return ret;
    prec->nord = uaItem->arraySize;
    uaItem->arraySize = prec->nelm; //FIXME: Is that really useful at every processing? NELM never changes.
    prec->udf=FALSE;

    try{
        UaVariant &val = uaItem->varVal;
        if(val.isArray()){
            UaByteArray   aByte;
            UaInt16Array  aInt16;
            UaUInt16Array aUInt16;
            UaInt32Array  aInt32;
            UaUInt32Array aUInt32;
            UaFloatArray  aFloat;
            UaDoubleArray aDouble;

            if(val.arraySize() <= uaItem->arraySize) {
                switch(uaItem->recDataType) {
                case epicsInt8T:
                case epicsUInt8T:
                    val.toByteArray( aByte);
                    memcpy(prec->bptr,aByte.data(),sizeof(epicsInt8)*uaItem->arraySize);
                    break;
                case epicsInt16T:
                    val.toInt16Array( aInt16);
                    memcpy(prec->bptr,aInt16.rawData(),sizeof(epicsInt16)*uaItem->arraySize);
                    break;
                case epicsEnum16T:
                case epicsUInt16T:
                    val.toUInt16Array( aUInt16);
                    memcpy(prec->bptr,aUInt16.rawData(),sizeof(epicsUInt16)*uaItem->arraySize);
                    break;
                case epicsInt32T:
                    val.toInt32Array( aInt32);
                    memcpy(prec->bptr,aInt32.rawData(),sizeof(epicsInt32)*uaItem->arraySize);
                    break;
                case epicsUInt32T:
                    val.toUInt32Array( aUInt32);
                    memcpy(prec->bptr,aUInt32.rawData(),sizeof(epicsUInt32)*uaItem->arraySize);
                    break;
                case epicsFloat32T:
                    val.toFloatArray( aFloat);
                    memcpy(prec->bptr,aFloat.rawData(),sizeof(epicsFloat32)*uaItem->arraySize);
                    break;
                case epicsFloat64T:
                    val.toDoubleArray( aDouble);
                    memcpy(prec->bptr,aDouble.rawData(),sizeof(epicsFloat64)*uaItem->arraySize);
                    break;
                default:
                    if(uaItem->debug >= 2) errlogPrintf("%s setRecVal(): Can't convert array data type\n",uaItem->prec->name);
                    return 1;
                }
            }
            else {
                if(uaItem->debug >= 2) errlogPrintf("%s setRecVal() Error record arraysize %d < OpcItem Size %d\n", uaItem->prec->name,val.arraySize(),uaItem->arraySize);
                return 1;
            }
        }      // end array
    }
    catch(...) {
        errlogPrintf("%s Unexpected Exception in  read_wf()\n",uaItem->prec->name);
        ret = 1;
    }
    epicsMutexUnlock(uaItem->flagLock);
    if(DEBUG_LEVEL >= 2) errlogPrintf("read_wf     %s %s NELM:%d\n",prec->name,getTime(buf),prec->nelm);
    if(ret)
        recGblSetSevr(prec,menuAlarmStatREAD,menuAlarmSevrINVALID);
    return ret;
}

/* callback service routine */
static void outRecordCallback(CALLBACK *pcallback) {
    char buf[256];
    void *pVoid;
    dbCommon *prec;
    OPCUA_ItemINFO* uaItem;
    typedef long Process(dbCommon*);
    Process *procFunc;

    callbackGetUser(pVoid, pcallback);
    if(!pVoid)
        return;
    prec = (dbCommon*) pVoid;
    procFunc = (Process*)prec->rset->process;
    uaItem = (OPCUA_ItemINFO*)prec->dpvt;

    dbScanLock(prec);
    if(prec->pact == TRUE) {        // waiting for async write operation to be finished. Try again later
        if(DEBUG_LEVEL >= 3) errlogPrintf("write Callb:  %s %s PACT:%d varVal:%s uaItem->stat:%d, RdbkOff:%d, IsRdbk:%d\n", getTime(buf),prec->name,prec->pact,uaItem->varVal.toString().toUtf8(),uaItem->stat,uaItem->flagRdbkOff,uaItem->flagIsRdbk);
        procFunc(prec);
    }
    else {
        uaItem->flagIsRdbk = 1;
        prec->udf=FALSE;
        if(DEBUG_LEVEL >= 3) errlogPrintf("rdbk Callb:  %s %s PACT:%d varVal:%s uaItem->stat:%d, RdbkOff:%d, IsRdbk:%d\n", getTime(buf),prec->name,prec->pact,uaItem->varVal.toString().toUtf8(),uaItem->stat,uaItem->flagRdbkOff,uaItem->flagIsRdbk);
        procFunc(prec);
        uaItem->flagIsRdbk = 0;
    }
    dbScanUnlock(prec);
}

static long get_ioint_info(int cmd, dbCommon *prec, IOSCANPVT * ppvt) {
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    if(!prec || !prec->dpvt)
        return 1;
    *ppvt = uaItem->ioscanpvt;
    if(DEBUG_LEVEL >= 2) errlogPrintf("get_ioint_info %s %s I/O event list - ioscanpvt=%p\n",
                     prec->name, cmd?"removed from":"added to", *ppvt);
    return 0;
}

/* Setup commons for all record types: debug level, alarms. Don't deal with the value! */
static long read(dbCommon * prec) {
    long ret = 0;
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    try {
        if(!uaItem) {
                errlogPrintf("%s read error uaItem = 0\n", prec->name);
                return 1;
            }
            uaItem->debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1

            ret = uaItem->stat;

            if(!ret)
                prec->udf=FALSE;
    }
    catch(...) {
        errlogPrintf("%s: Exception in devOpcUa read() val=%s %s",prec->name,uaItem->varVal.toString().toUtf8(),variantTypeStrings(uaItem->itemDataType));
        ret = 1;
    }
    if(ret) {
        recGblSetSevr(prec,menuAlarmStatREAD,menuAlarmSevrINVALID);
        if(DEBUG_LEVEL>0) errlogPrintf("%s\tread() failed item->stat:%d\n",uaItem->prec->name,uaItem->stat);
    }
    return ret;
}

static long write(dbCommon *prec,UaVariant &var) {
    long ret = 0;
    OPCUA_ItemINFO* uaItem = (OPCUA_ItemINFO*)prec->dpvt;
    if(!prec->pact) {
        prec->pact = TRUE;
        try {
            if(DEBUG_LEVEL >= 2) errlogPrintf("write()\t\tflagIsRdbk=%i\n",uaItem->flagIsRdbk);
            if( ! uaItem->flagIsRdbk ) {
                epicsMutexLock(uaItem->flagLock);
                ret = uaItem->write(var);   // write on a read only node results NOT to isBad(). Can't be checked here!!
                epicsMutexUnlock(uaItem->flagLock);
            }
        }
        catch(...) {
            errlogPrintf("%s: Exception in devOpcUa write() val=%s %s",prec->name,var.toString().toUtf8(),variantTypeStrings(uaItem->itemDataType));
            ret=1;
        }
    }
    else {
        ret = uaItem->stat; // 1 if writeComplete failed
    }

    return ret;
}

