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

#ifndef __UAITEM_H
#define __UAITEM_H

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

class UaItem;

#include "devUaSession.h"
#include "devUaSubscription.h"

#define ITEMPATHLEN 128
class UaItem {
public:
    UaItem(dbCommon *prec);
    bool parseLink(char *lnk);  // Parse link,setup pClient, nodeId, tag and pClient
    int  maxDebug(int recDbg);
    int  checkDataLoss();
    long write(UaVariant &tempValue);
    inline char *getPv(void) {return prec->name;}
    char *ItemPath;

    int itemDataType;       // OPCUA Datatype
    int itemIdx;            // Index of this item in UaNodeId vector

    epicsUInt32 userAccLvl; // UserAcessLevel: write=2, read=1, rw=3
    UaVariant varVal;       // buffer to hold the value got from Opc
    UaNodeId  nodeId;       // The unique opcua node id on the server
    std::string tag;        // identify the subscription or client for scanned records
    DevUaSession* pSession;

    epicsType recDataType;  // Data type of the records VAL/RVAL field
    void *pInpVal;          // Input field to set OUT-records by the opcUa server.
    epicsType inpDataType;  // OUT records: the type of the records input = VAL field - may differ from RVAL type!.
                            // Allways set to 0 for INP records! 0=epicsInt8T is not in use for a record.

    int isArray;            // array record
    int arraySize;          // record array size

    // OPC UA properties of the monitored item
    double samplingInterval;
    epicsUInt32 queueSize;
    unsigned char discardOldest;

    int debug;              // debug level of this item, defined in field REC:TPRO

    OpcUa_StatusCode stat;  // status of the last operation on the item 0=OpcGood, OpcUa_StatusCode or 1 for any internal error
    epicsMutexId flagLock;  // mutex for lock flag access
    int flagIsRdbk;         // OUT-record flag to signal the dbProcess a value to readback by dataChange callback
    int flagRdbkOff;        // OUT-record flag > 0 causes the dataChange callback NOT to process the record.
                            // bit_1: Set temporary if an ao-record is in step towards its value in OROC steps
                            // bit_0: Set by constantly by the info field RDBKOFF

    IOSCANPVT ioscanpvt;    // in-records scan request.
    CALLBACK callback;      // out-records callback request.
    dbCommon *prec;

};

#endif /* ifndef __UAITEM_H */
