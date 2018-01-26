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

#ifndef __DRVOPCUA_H
#define __DRVOPCUA_H

#include <dbCommon.h>
#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <errlog.h>
#include <dbScan.h>
#include <callback.h>

#include <uaplatformlayer.h>
#include <uabase.h>
#include <uaclientsdk.h>
#include <uasession.h>

class OPCUA_ItemINFO;
#include "devUaClient.h"
#include "devUaSubscription.h"

#define ANY_VAL_STRING_SIZE 80
typedef union {                     /* A subset of the built in types we use */
        epicsInt32   Int32;
        epicsUInt32  UInt32;
        epicsFloat64 Double;
//        char        *cString;     /* need a buffer to receive strings, see sampleSubscription.cpp! */
        char         cString[ANY_VAL_STRING_SIZE];   /* find max defined stringsize of base: */
} epicsAnyVal;                      // perl -ne 'print "$2 $1\n" if($_=~/char\s+([\w\d_]+)\[(\d+)\]/);' base-3.14.12.5/include/*|sort -u

#define ITEMPATHLEN 128
class OPCUA_ItemINFO {
public:
//    int NdIdx;              // Namspace index
    char ItemPath[ITEMPATHLEN];

    int itemDataType;       // OPCUA Datatype
    int itemIdx;            // Index of this item in UaNodeId vector

    epicsUInt32 userAccLvl; // UserAcessLevel: write=2, read=1, rw=3
    epicsAnyVal varVal;     // buffer to hold the value got from Opc for all scalar values, including string

    void *pRecVal;          // point to records val/rval/oval field
    epicsType recDataType;  // Data type of the records VAL/RVAL field

    void *pInpVal;          // Input field to set OUT-records by the opcUa server
    epicsType inpDataType;  // OUT records: the type of the records input = VAL field - may differ from RVAL type!. INP records = NULL

    epicsMutexId flagLock;  // mutex for lock flag access

    int isArray;
    int arraySize;
                            // OPC UA properties of the monitored item
    double samplingInterval;
    epicsUInt32 queueSize;
    unsigned char discardOldest;

    int debug;              // debug level of this item, defined in field REC:TPRO
    int stat;               // Status of the opc connection
    int flagSuppressWrite;  // flag for OUT-records: prevent write back of incomming values

    IOSCANPVT ioscanpvt;    // in-records scan request.
    CALLBACK callback;      // out-records callback request.

    dbCommon *prec;
};
extern DevUaClient* pMyClient;
typedef enum {BOTH=0,NODEID,BROWSEPATH,BROWSEPATH_CONCAT,GETNODEMODEMAX} GetNodeMode;
const  char *variantTypeStrings(int type);
extern char *getTime(char *buf);
extern long opcUa_close(int verbose);
extern long OpcUaSetupMonitors(void);
extern void addOPCUA_Item(OPCUA_ItemINFO *h);
// iocShell:
extern long OpcUaWriteItems(OPCUA_ItemINFO* uaItem);

// client:
extern long OpcReadValues(int verbose,int monitored);
extern long OpcWriteValue(int opcUaItemIndex,double val,int verbose);
extern int  maxDebug(int dbg,int recDbg);

extern long setRecVal(const UaVariant &val, OPCUA_ItemINFO* uaItem,int debug);
extern long opcUa_init(UaString &g_serverUrl, UaString &g_applicationCertificate, UaString &g_applicationPrivateKey, UaString &nodeName, int autoConn, int debug);
extern "C" {
extern long opcUa_io_report (int); /* Write IO report output to stdout. */
}

#endif /* ifndef __DRVOPCUA_H */
