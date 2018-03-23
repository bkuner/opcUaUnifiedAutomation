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

#include <stdlib.h>
#include <csignal>

#include <boost/algorithm/string.hpp>
#include <string>
#include <vector>

// regex and stoi for lexical_cast are available as std functions in C11
//#include <regex> 
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

#include <epicsPrint.h>
#include <epicsExport.h>
#include <registryFunction.h>
#include <dbCommon.h>
#include <devSup.h>
#include <drvSup.h>
#include <devLib.h>
#include <iocsh.h>

#include "drvOpcUa.h"
#include "devUaSubscription.h"
#include "devUaClient.h"

using namespace UaClientSdk;

// Wrapper to ignore return values
template<typename T>
inline void ignore_result(T /* unused result */) {}

char *getTime(char *timeBuffer)
{
    epicsTimeStamp ts;
    epicsTimeGetCurrent(&ts);
    epicsTimeToStrftime(timeBuffer,28,"%y-%m-%dT%H:%M:%S.%06f",&ts);
    return timeBuffer;
}



const char *variantTypeStrings(int type)
{
    switch(type) {
        case 0:  return "OpcUa_Null";
        case 1:  return "OpcUa_Boolean";
        case 2:  return "OpcUa_SByte";
        case 3:  return "OpcUa_Byte";
        case 4:  return "OpcUa_Int16";
        case 5:  return "OpcUa_UInt16";
        case 6:  return "OpcUa_Int32";
        case 7:  return "OpcUa_UInt32";
        case 8:  return "OpcUa_Int64";
        case 9:  return "OpcUa_UInt64";
        case 10: return "OpcUa_Float";
        case 11: return "OpcUa_Double";
        case 12: return "OpcUa_String";
        case 13: return "OpcUa_DateTime";
        case 14: return "OpcUa_Guid";
        case 15: return "OpcUa_ByteString";
        case 16: return "OpcUa_XmlElement";
        case 17: return "OpcUa_NodeId";
        case 18: return "OpcUa_ExpandedNodeId";
        case 19: return "OpcUa_StatusCode";
        case 20: return "OpcUa_QualifiedName";
        case 21: return "OpcUa_LocalizedText";
        case 22: return "OpcUa_ExtensionObject";
        case 23: return "OpcUa_DataValue";
        case 24: return "OpcUa_Variant";
        case 25: return "OpcUa_DiagnosticInfo";
        default: return "Illegal Value";
    }
}

//inline int64_t getMsec(DateTime dateTime){ return (dateTime.Value % 10000000LL)/10000; }


void printVal(UaVariant &val,OpcUa_UInt32 IdxUaItemInfo);
void print_OpcUa_DataValue(_OpcUa_DataValue *d);


// global variables

DevUaClient* pMyClient = NULL;

extern "C" {
                                    /* DRVSET */
    struct {
        long      number;
        DRVSUPFUN report;
        DRVSUPFUN init;
    }  drvOpcUa = {
        2,
        (DRVSUPFUN) opcUa_io_report,
        NULL
    };
    epicsExportAddress(drvet,drvOpcUa);


    epicsRegisterFunction(opcUa_io_report);
    long opcUa_io_report (int level) /* Write IO report output to stdout. */
    {
        pMyClient->itemStat(level);
        return 0;
    }
}

// proper close of connections
void signalHandler( int sig ) {
    //opcUa_close(1);
    errlogPrintf("Done.\n");
    exit(0);
}

// Maximize debug level driver-dbg (active >=1) and uaItem.debug set by record.TPRO
int OPCUA_ItemINFO::maxDebug(int dbg) {
    return (debug>dbg)?debug:dbg;
}

// return 0:ok, 1:data loss may occur, 2:not supported OPCUA-Type
// supposed EPICS-types: double, int32, uint32. As long as VAL,RVAL fields have no other types!
int OPCUA_ItemINFO::checkDataLoss()
{
    epicsType epicstype;
    if(inpDataType) {   // is OUT Record. TODO: what about records data conversion?
        epicstype = inpDataType;
    }
    else {
        epicstype = recDataType;
    }
    if(inpDataType) { // data loss warning for OUT-records
        switch(itemDataType){
        case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
            switch(epicstype){
            case epicsFloat64T: return 1;
            default:;
            }
            break;
        case OpcUaType_SByte:
        case OpcUaType_Byte:
        case OpcUaType_Int16:
        case OpcUaType_UInt16: return 1;
            break;

        case OpcUaType_Int32:
            switch(epicstype){//REC_Datatype(EPICS_Datatype)
            case epicsFloat64T: return 1;
            default:;
            }
            break;
        case OpcUaType_UInt32:
            switch(epicstype){//REC_Datatype(EPICS_Datatype)
            case epicsFloat64T: return 1;  break;
            default:;
            }
            break;
        case OpcUaType_Float:
            switch(epicstype){//REC_Datatype(EPICS_Datatype)
            case epicsFloat64T: return 1;  break;
            default:;
            }
            break;
        case OpcUaType_Double:
            break;
        case OpcUaType_String:
            if(epicstype != epicsOldStringT)
                return 1;
            break;
        default:
            return 2;
        }
    }
    else { // data loss warning for IN-records
        switch(epicstype){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:
            switch(itemDataType){
            case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
            case OpcUaType_SByte:
            case OpcUaType_Byte:
            case OpcUaType_Int16:
            case OpcUaType_UInt16:
            case OpcUaType_Int32:
            case OpcUaType_UInt32: return 0;
            case OpcUaType_Float:
            case OpcUaType_Double:
            case OpcUaType_String: return 1;
            default:
                return 2;
            }
        case epicsUInt32T:
                switch(itemDataType){
                case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
                case OpcUaType_SByte:
                case OpcUaType_Byte:
                case OpcUaType_Int16:
                case OpcUaType_UInt16:
                case OpcUaType_UInt32: return 0;
                case OpcUaType_Int32:
                case OpcUaType_Float:
                case OpcUaType_Double:
                case OpcUaType_String: return 1;
                default:
                    return 2;
                }
        case epicsFloat64T: return 0;
                switch(itemDataType){
                case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
                case OpcUaType_SByte:
                case OpcUaType_Byte:
                case OpcUaType_Int16:
                case OpcUaType_UInt16:
                case OpcUaType_UInt32:
                case OpcUaType_Int32:
                case OpcUaType_Float:
                case OpcUaType_Double: return 0;
                case OpcUaType_String: return 1;
                default:
                    return 2;
                }
        case epicsOldStringT:
            if(itemDataType != OpcUaType_String)
                return 1;
            break;
        default: return 2;
        }

    }
    return 0;
}

/***************** just for debug ********************/

void print_OpcUa_DataValue(_OpcUa_DataValue *d)
{
    if (OpcUa_IsGood(d->StatusCode)) {
        errlogPrintf("Datatype: %d ArrayType:%d SourceTS: H%d,L%d,Pico%d ServerTS: H%d,L%d,Pico%d",
               (d->Value).Datatype,(d->Value).ArrayType,
                (d->SourceTimestamp).dwLowDateTime, (d->SourceTimestamp).dwHighDateTime,d->SourcePicoseconds,
                (d->ServerTimestamp).dwLowDateTime, (d->ServerTimestamp).dwHighDateTime,d->ServerPicoseconds);
//        (d->Value).Value;
    }
    else {
        errlogPrintf("Statuscode BAD: %d %s",d->StatusCode,UaStatus(d->StatusCode).toString().toUtf8());
    }
    errlogPrintf("\n");

}

void printVal(UaVariant &val,OpcUa_UInt32 IdxUaItemInfo)
{
    int i;
    if(val.isArray()) {
        for(i=0;i<val.arraySize();i++) {
            if(UaVariant(val[i]).type() < OpcUaType_String)
                errlogPrintf("%s[%d] %s\n",pMyClient->vUaItemInfo[IdxUaItemInfo]->ItemPath,i,UaVariant(val[i]).toString().toUtf8());
            else
                errlogPrintf("%s[%d] '%s'\n",pMyClient->vUaItemInfo[IdxUaItemInfo]->ItemPath,i,UaVariant(val[i]).toString().toUtf8());
        }
    }
    else {
        if(val.type() < OpcUaType_String)
            errlogPrintf("%s %s\n",pMyClient->vUaItemInfo[IdxUaItemInfo]->ItemPath, val.toString().toUtf8());
        else
            errlogPrintf("%s '%s'\n",pMyClient->vUaItemInfo[IdxUaItemInfo]->ItemPath, val.toString().toUtf8());
    }
}

long OPCUA_ItemINFO::write(UaVariant &tempValue)
{
    UaStatus status = pMyClient->writeFunc(this, tempValue);
    if ( status.isBad()  )              // write on a read only node is not bad! Can't be checked here!!
    {
        if(pMyClient->getDebug()) errlogPrintf("%s\tOpcUaWriteItems: UaSession::write failed [ret=%s] **\n",prec->name,status.toString().toUtf8());
        return 1;
    }
    return 0;
}

/* iocShell: Read and setup uaItem Item data type, createMonitoredItems */
extern "C" {
epicsRegisterFunction(OpcUaSetupMonitors);
}
long OpcUaSetupMonitors(void)
{
    UaStatus status;
    UaDataValues values;
    UaDataValues attribs; // OpcUa_Attributes_UserAccessLevel
    ServiceSettings     serviceSettings;
    UaDiagnosticInfos   diagnosticInfos;

    if(pMyClient == NULL)
        return 1;
    if(pMyClient->getDebug()) errlogPrintf("OpcUaSetupMonitors Browsepath ok len = %d\n",(int)pMyClient->vUaNodeId.size());

    if(pMyClient->getNodes() )
        return 1;
    status = pMyClient->readFunc(values, serviceSettings, diagnosticInfos,OpcUa_Attributes_Value);
    if (status.isBad()) {
        errlogPrintf("OpcUaSetupMonitors: READ VALUES failed with status %s\n", status.toString().toUtf8());
        return 1;
    }
    status = pMyClient->readFunc(attribs, serviceSettings, diagnosticInfos, OpcUa_Attributes_UserAccessLevel);
    if (status.isBad()) {
        errlogPrintf("OpcUaSetupMonitors: READ VALUES failed with status %s\n", status.toString().toUtf8());
        return 1;
    }
    if(pMyClient->getDebug() > 1) errlogPrintf("OpcUaSetupMonitors READ of %d values returned ok\n", values.length());
    for(OpcUa_UInt32 i=0; i<values.length(); i++) {
        OPCUA_ItemINFO* uaItem = pMyClient->vUaItemInfo[i];
        if (OpcUa_IsBad(values[i].StatusCode)) {
            uaItem->stat = 1;
            errlogPrintf("%s: Read node '%s' failed with status %s\n",uaItem->prec->name, uaItem->ItemPath,
                         UaStatus(values[i].StatusCode).toString().toUtf8());
        }
        else {
            if(OpcUa_IsBad(attribs[i].StatusCode)) {
                uaItem->stat = 1;
                errlogPrintf("%s: Read attribs' failed with status %s\n",uaItem->prec->name,
                             UaStatus(attribs[i].StatusCode).toString().toUtf8());
            }
            else {
                UaVariant var = attribs[i].Value;
                var.toUInt32(uaItem->userAccLvl);
            }
            epicsMutexLock(uaItem->flagLock);
            uaItem->itemDataType = (int) values[i].Value.Datatype;
            epicsMutexUnlock(uaItem->flagLock);

            uaItem->stat = ((uaItem->stat == 0) & ((int)values[i].Value.ArrayType == uaItem->isArray)) ? 0 : 1;
            if(pMyClient->getDebug() > 0) {
                if(uaItem->checkDataLoss()) {
                    if ((int) uaItem->inpDataType) // OUT-Record
                        errlogPrintf("%20s: write may loose data: %s -> %s\n",uaItem->prec->name,epicsTypeNames[uaItem->recDataType],
                            variantTypeStrings(uaItem->itemDataType));
                    else
                        errlogPrintf("%20s: read may loose data: %s -> %s\n",uaItem->prec->name,epicsTypeNames[uaItem->recDataType],
                            variantTypeStrings(uaItem->itemDataType));
                }
                if( (uaItem->userAccLvl & 0x2) == 0 && ((int) uaItem->inpDataType))    // no write access to out record
                    errlogPrintf("%20s: no write Access!\n",uaItem->prec->name);
                if( !(uaItem->userAccLvl & 0x1) )                                  // no read access
                    errlogPrintf("%20s: no read Access!\n",uaItem->prec->name);
                if(pMyClient->getDebug() > 3) errlogPrintf("%4d %15s %p\n",uaItem->itemIdx,uaItem->prec->name,uaItem);
            }
        }
    }
    pMyClient->createMonitoredItems();
    return 0;
}

/* iocShell/Client: unsubscribe, disconnect from server */
long opcUa_close(int verbose)
{
    UaStatus status;
    if(verbose) errlogPrintf("opcUa_close()\n\tunsubscribe\n");

    if(pMyClient == NULL)
        return 1;
    status = pMyClient->unsubscribe();
    if(verbose) errlogPrintf("\tdisconnect\n");
    status = pMyClient->disconnect();

    delete pMyClient;
    pMyClient = NULL;

    if(verbose) errlogPrintf("\tcleanup\n");
    UaPlatformLayer::cleanup();
    return 0;
}

/* iocShell/Client: Setup an opcUa Item for the driver*/
int addOPCUA_Item(OPCUA_ItemINFO *h)
{
    if(pMyClient == NULL)
        return 1;
    pMyClient->addOPCUA_Item(h);
    return 0;
}

/* iocShell/Client: Setup server url and certificates, connect and subscribe */
long opcUa_init(UaString &g_serverUrl, UaString &g_applicationCertificate, UaString &g_applicationPrivateKey, UaString &nodeName, int autoConn,int debug=0)
{
    UaStatus status;
    // Initialize the UA Stack platform layer
    UaPlatformLayer::init();

    // Create instance of DevUaClient
    pMyClient = new DevUaClient(autoConn,debug);

    pMyClient->applicationCertificate = g_applicationCertificate;
    pMyClient->applicationPrivateKey  = g_applicationPrivateKey;
    pMyClient->hostName = nodeName;
    pMyClient->url = g_serverUrl;
    pMyClient->setDebug(debug);
    // Connect to OPC UA Server
    status = pMyClient->connect();
    if(status.isBad()) {
        errlogPrintf("drvOpcuaSetup: Failed to connect to server '%s' - will retry every %f sec\n",
                     g_serverUrl.toUtf8(), pMyClient->drvOpcua_AutoConnectInterval);
        return 1;
    }
    // Create subscription
    status = pMyClient->subscribe();
    if(status.isBad()) {
        errlogPrintf("drvOpcuaSetup: Failed to subscribe to server '%s'\n", g_serverUrl.toUtf8());
        return 1;
    }
    return 0;
}

/* iocShell: shell functions */

static const iocshArg drvOpcuaSetupArg0 = {"[URL] to server", iocshArgString};
static const iocshArg drvOpcuaSetupArg1 = {"[CERT_PATH] optional", iocshArgString};
static const iocshArg drvOpcuaSetupArg2 = {"[HOST] optional", iocshArgString};
static const iocshArg drvOpcuaSetupArg3 = {"Debug Level for library", iocshArgInt};
static const iocshArg *const drvOpcuaSetupArg[4] = {&drvOpcuaSetupArg0,&drvOpcuaSetupArg1,&drvOpcuaSetupArg2,&drvOpcuaSetupArg3};
iocshFuncDef drvOpcuaSetupFuncDef = {"drvOpcuaSetup", 4, drvOpcuaSetupArg};
void drvOpcuaSetup (const iocshArgBuf *args )
{
    UaString g_serverUrl;
    UaString g_certificateStorePath;
    UaString g_defaultHostname("unknown_host");
    UaString g_applicationCertificate;
    UaString g_applicationPrivateKey;

    if(args[0].sval == NULL)
    {
      errlogPrintf("drvOpcuaSetup: ABORT Missing Argument \"url\".\n");
      return;
    }
    g_serverUrl = args[0].sval;

    if(args[1].sval == NULL)
    {
      errlogPrintf("drvOpcuaSetup: ABORT Missing Argument \"cert path\".\n");
      return;
    }
    g_certificateStorePath = args[1].sval;

    if(args[2].sval == NULL)
    {
      errlogPrintf("drvOpcuaSetup: ABORT Missing Argument \"host name\".\n");
      return;
    }

    char szHostName[256];
    if (0 == UA_GetHostname(szHostName, 256))
    {
        g_defaultHostname = szHostName;
    }
    else
        if(strlen(args[2].sval) > 0)
            g_defaultHostname = args[2].sval;

    g_certificateStorePath = args[1].sval;
    int verbose = args[4].ival;

    if(verbose) {
        errlogPrintf("Host:\t'%s'\n",g_defaultHostname.toUtf8());
        errlogPrintf("URL:\t'%s'\n",g_serverUrl.toUtf8());
    }
    if(g_certificateStorePath.size() > 0) {
        g_applicationCertificate = g_certificateStorePath + "/certs/cert_client_" + g_defaultHostname + ".der";
        g_applicationPrivateKey	 = g_certificateStorePath + "/private/private_key_client_" + g_defaultHostname + ".pem";
        if(verbose) {
            errlogPrintf("Set certificate path:\n\t'%s'\n",g_certificateStorePath.toUtf8());
            errlogPrintf("Client Certificate:\n\t'%s'\n",g_applicationCertificate.toUtf8());
            errlogPrintf("Client privat key:\n\t'%s'\n",g_applicationPrivateKey.toUtf8());
        }
    }

    opcUa_init(g_serverUrl,g_applicationCertificate,g_applicationPrivateKey,g_defaultHostname,1,verbose);
}
extern "C" {
epicsRegisterFunction(drvOpcuaSetup);
}

static const iocshArg opcuaDebugArg0 = {"Debug Level for library", iocshArgInt};
static const iocshArg *const opcuaDebugArg[1] = {&opcuaDebugArg0};
iocshFuncDef opcuaDebugFuncDef = {"opcuaDebug", 1, opcuaDebugArg};
void opcuaDebug (const iocshArgBuf *args )
{
    if(pMyClient)
        pMyClient->setDebug(args[0].ival);
    else
        errlogPrintf("Ignore: OpcUa not initialized\n");
    return;
}
extern "C" {
epicsRegisterFunction(opcuaDebug);
}

static const iocshArg opcuaStatArg0 = {"Verbosity Level", iocshArgInt};
static const iocshArg *const opcuaStatArg[1] = {&opcuaStatArg0};
iocshFuncDef opcuaStatFuncDef = {"opcuaStat", 1, opcuaStatArg};
void opcuaStat (const iocshArgBuf *args )
{
    if(pMyClient!= NULL)
        pMyClient->itemStat(args[0].ival);
    return;
}
extern "C" {
epicsRegisterFunction(opcuaStat);
}

//create a static object to make shure that opcRegisterToIocShell is called on beginning of
class OpcRegisterToIocShell
{
public :
        OpcRegisterToIocShell(void);
};

OpcRegisterToIocShell::OpcRegisterToIocShell(void)
{
    iocshRegister(&drvOpcuaSetupFuncDef, drvOpcuaSetup);
    iocshRegister(&opcuaDebugFuncDef, opcuaDebug);
    iocshRegister(&opcuaStatFuncDef, opcuaStat);
      //
}
static OpcRegisterToIocShell opcRegisterToIocShell;

