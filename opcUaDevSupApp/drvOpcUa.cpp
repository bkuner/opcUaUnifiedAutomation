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
#include <map>
#include <iterator>

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
#include "devUaSession.h"

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
        DevUaSession::opcuaStat(level);
        return 0;
    }
}

// proper close of connections
void signalHandler( int sig ) {
    opcUa_close(1);
    sleep(2);
    errlogPrintf("Done.\n");
    exit(0);
}


/* Unsubscribe, disconnect from server */
long opcUa_close(int debug)
{
    return DevUaSession::opcuaClose(debug);
}


/* iocShell: shell functions */

static const iocshArg drvOpcuaSetupArg0 = {"[URL] to server", iocshArgString};
static const iocshArg drvOpcuaSetupArg1 = {"[CERT_PATH] optional", iocshArgString};
static const iocshArg drvOpcuaSetupArg2 = {"[HOST] optional", iocshArgString};
static const iocshArg drvOpcuaSetupArg3 = {"Debug Level for library", iocshArgInt};
static const iocshArg drvOpcuaSetupArg4 = {"Server tag", iocshArgString};
static const iocshArg *const drvOpcuaSetupArg[5] = {&drvOpcuaSetupArg0,&drvOpcuaSetupArg1,&drvOpcuaSetupArg2,&drvOpcuaSetupArg3,&drvOpcuaSetupArg4};
iocshFuncDef drvOpcuaSetupFuncDef = {"drvOpcuaSetup", 5, drvOpcuaSetupArg};
void drvOpcuaSetup (const iocshArgBuf *args )
{
    UaString g_serverUrl;
    UaString g_certificateStorePath;
    UaString hostname("unknown_host");
    UaString g_applicationCertificate;
    UaString g_applicationPrivateKey;
    std::string serverTag;

    // Initialize the UA Stack platform layer
    UaPlatformLayer::init();

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
        hostname = szHostName;
    }
    else
        if(strlen(args[2].sval) > 0)
            hostname = args[2].sval;

    g_certificateStorePath = args[1].sval;

    int debug = args[3].ival;

    if(strlen(args[4].sval) > 0)
        serverTag = args[4].sval;
    if(DevUaSession::getSession(serverTag) != NULL) {
        errlogPrintf("Error: Skip session tag '%s', already in use\n",serverTag.c_str());
        return;
    }

    if(debug) {
        errlogPrintf("Host:\t'%s'\n",hostname.toUtf8());
        errlogPrintf("URL:\t'%s'\n",g_serverUrl.toUtf8());
        errlogPrintf("ServerTag:\t'%s'\n",serverTag.c_str());
    }
    if(g_certificateStorePath.size() > 0) {
        g_applicationCertificate = g_certificateStorePath + "/certs/cert_client_" + hostname + ".der";
        g_applicationPrivateKey	 = g_certificateStorePath + "/private/private_key_client_" + hostname + ".pem";
        if(debug) {
            errlogPrintf("Set certificate path:\n\t'%s'\n",g_certificateStorePath.toUtf8());
            errlogPrintf("Client Certificate:\n\t'%s'\n",g_applicationCertificate.toUtf8());
            errlogPrintf("Client privat key:\n\t'%s'\n",g_applicationPrivateKey.toUtf8());
        }
    }

    // Create instance of DevUaClient
    DevUaSession* pSession = DevUaSession::getSession(serverTag);
    if(pSession != NULL) {
        pSession = new DevUaSession(serverTag,1,debug,10);
        UaStatus status;

        pSession->applicationCertificate = g_applicationCertificate;
        pSession->applicationPrivateKey  = g_applicationPrivateKey;
        pSession->hostName = hostname;
        pSession->url = g_serverUrl;
        // Connect to OPC UA Server
        status = pSession->connect();
        if(status.isBad()) {
            errlogPrintf("drvOpcuaSetup: Failed to connect to server '%s' - will retry every %f sec\n",
                         g_serverUrl.toUtf8(), pSession->drvOpcua_AutoConnectInterval);
            return;
        }
    }
    else
        errlogPrintf("Error: Server tag '%s' allready defined, must be unique!\n",serverTag.c_str());
}
extern "C" {
epicsRegisterFunction(drvOpcuaSetup);
}

static const iocshArg drvOpcuaSubscriptionArg0 = {"Subscription tag", iocshArgString};
static const iocshArg drvOpcuaSubscriptionArg1 = {"Server tag", iocshArgString};
static const iocshArg drvOpcuaSubscriptionArg2 = {"Options list [NAME=VALUE,...]", iocshArgString};
static const iocshArg *const drvOpcuaSubscriptionArg[3] = {&drvOpcuaSubscriptionArg0,&drvOpcuaSubscriptionArg1,&drvOpcuaSubscriptionArg2};
iocshFuncDef drvOpcuaSubscriptionFuncDef = {"drvOpcuaSubscription", 3, drvOpcuaSubscriptionArg};
void drvOpcuaSubscription (const iocshArgBuf *args )
{
    std::string subscrTag = args[0].sval;
    std::string serverTag = args[1].sval;
    std::string options   = args[2].sval;
    long status;

    DevUaSession* pSession = DevUaSession::getSession(serverTag);
    if(pSession == NULL) {
        errlogPrintf("Error: server tag '%s' not defined!\n",serverTag.c_str());
        return;
    }

    // Create subscription
    status = pSession->addSubscription(subscrTag);
    if(status) {
        errlogPrintf("\t Failed to subscribe to server '%s'\n", serverTag.c_str());
    }
    return;
}
extern "C" {
epicsRegisterFunction(drvOpcuaSubscription);
}

static const iocshArg opcuaDebugArg0 = {"Debug Level for library", iocshArgInt};
static const iocshArg *const opcuaDebugArg[1] = {&opcuaDebugArg0};
iocshFuncDef opcuaDebugFuncDef = {"opcuaDebug", 1, opcuaDebugArg};
void opcuaDebug (const iocshArgBuf *args )
{
    DevUaSession::opcuaSetDebug(args[0].ival);
}
extern "C" {
epicsRegisterFunction(opcuaDebug);
}

static const iocshArg opcuaStatArg0 = {"Verbosity Level", iocshArgInt};
static const iocshArg *const opcuaStatArg[1] = {&opcuaStatArg0};
iocshFuncDef opcuaStatFuncDef = {"opcuaStat", 1, opcuaStatArg};
void opcuaStat (const iocshArgBuf *args )
{
    DevUaSession::opcuaStat(args[0].ival);
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
    iocshRegister(&drvOpcuaSubscriptionFuncDef, drvOpcuaSubscription);
    iocshRegister(&opcuaDebugFuncDef, opcuaDebug);
    iocshRegister(&opcuaStatFuncDef, opcuaStat);
      //
}
static OpcRegisterToIocShell opcRegisterToIocShell;

