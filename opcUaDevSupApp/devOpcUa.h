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
#ifndef INCdevOpcUaH
#define INCdevOpcUaH

#include <dbScan.h>
#include <callback.h>
#include <epicsTypes.h>
#include <epicsMutex.h>


typedef union {                     /* A subset of the built in types we use */
        epicsInt32   Int32;
        epicsUInt32  UInt32;
        epicsFloat64 Double;
//        char        *cString;     /* need a buffer to receive strings, see sampleSubscription.cpp! */
        char         cString[80];   /* find max defined stringsize of base: */
} epicsAnyVal;                      // perl -ne 'print "$2 $1\n" if($_=~/char\s+([\w\d_]+)\[(\d+)\]/);' base-3.14.12.5/include/*|sort -u

#define ITEMPATHLEN 128
typedef struct OPCUA_Item {

//    int NdIdx;              // Namspace index
    char ItemPath[ITEMPATHLEN];

    int itemDataType;       /* OPCUA Datatype */
    epicsType recDataType;  /* Data type of the records VAL/RVAL field */
    epicsType inpDataType;  /* OUT records: the type of the records input = VAL field, INP records = NULL */
    int itemIdx;            /* Index of this item in UaNodeId vector */

    epicsAnyVal varVal;     /* buffer to hold the value got from Opc for all scalar values, including string   */
    void *pRecVal;          /* point to records val/rval/oval field */
    void *pInpVal;          /* Input field to set OUT-records by the opcUa server */

    epicsMutexId flagLock;  /* mutex for lock flag access */
    int isCallback;         /* is IN-record with SCAN=IOINTR or any type of OUT record*/

    int isArray;
    int arraySize;

    int debug;              // debug level
    int stat;               /* Status of the opc connection */
    int noOut;              /* flag for OUT-records: prevent write back of incomming values */

    IOSCANPVT ioscanpvt;    /* in-records scan request.*/
    CALLBACK callback;      /* out-records callback request.*/

    dbCommon *prec;
    struct OPCUA_Item *next;/* It depends on the used opcUa library if the OPCUA_Items are organized as list or as vector. */

} OPCUA_ItemINFO;

#ifdef __cplusplus
extern "C" {
#endif
OPCUA_ItemINFO *getHead();
void setHead(OPCUA_ItemINFO *);
#ifdef __cplusplus
}
#endif

#endif
