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
#include <signal.h>

#include <boost/algorithm/string.hpp>
#include <string>
#include <vector>

// regex and stoi for lexical_cast are available as std functions in C11
//#include <regex> 
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

#define epicsTypesGLOBAL
#include <epicsTypes.h>
#include <epicsPrint.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <epicsExport.h>
#include <registryFunction.h>
#include <dbCommon.h>
#include <devSup.h>
#include <drvSup.h>
#include <devLib.h>
#include <iocsh.h>

#include <uaplatformlayer.h>
#include <uabase.h>
#include <uaclientsdk.h>
#include <uasession.h>

#include "drvOpcUa.h"
#include "devUaSubscription.h"

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


using namespace UaClientSdk;

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

inline const char *serverStatusStrings(UaClient::ServerStatus type)
{
    switch (type) {
    case UaClient::Disconnected:                      return "Disconnected";
    case UaClient::Connected:                         return "Connected";
    case UaClient::ConnectionWarningWatchdogTimeout:  return "ConnectionWarningWatchdogTimeout";
    case UaClient::ConnectionErrorApiReconnect:       return "ConnectionErrorApiReconnect";
    case UaClient::ServerShutdown:                    return "ServerShutdown";
    case UaClient::NewSessionCreated:                 return "NewSessionCreated";
    default:                                          return "Unknown Status Value";
    }
}

//inline int64_t getMsec(DateTime dateTime){ return (dateTime.Value % 10000000LL)/10000; }

class autoSessionConnect;

class DevUaClient : public UaSessionCallback
{
    UA_DISABLE_COPY(DevUaClient);
public:
    DevUaClient(int autocon,int debug);
    virtual ~DevUaClient();

    // UaSessionCallback implementation ----------------------------------------------------
    virtual void connectionStatusChanged(OpcUa_UInt32 clientConnectionId, UaClient::ServerStatus serverStatus);
    // UaSessionCallback implementation ------------------------------------------------------

    UaString applicationCertificate;
    UaString applicationPrivateKey;
    UaString hostName;
    UaString url;
    UaStatus connect();
    UaStatus disconnect();
    UaStatus subscribe();
    UaStatus unsubscribe();
    void setBadQuality();

    void addOPCUA_Item(OPCUA_ItemINFO *h);
    long getNodes();
    long getBrowsePathItem(OpcUa_BrowsePath &browsePaths,std::string &ItemPath,const char nameSpaceDelim,const char pathDelimiter);
    UaStatus createMonitoredItems();

    UaStatus readFunc(UaDataValues &values,ServiceSettings &serviceSettings,UaDiagnosticInfos &diagnosticInfos,int Attribute);

    UaStatus writeFunc(ServiceSettings &serviceSettings,UaWriteValues &nodesToWrite,UaStatusCodeArray &results,UaDiagnosticInfos &diagnosticInfos);
    void writeComplete(OpcUa_UInt32 transactionId,const UaStatus&result,const UaStatusCodeArray& results,const UaDiagnosticInfos& diagnosticInfos);

    void itemStat(int v);
    void setDebug(int debug);
    int  getDebug();

    /* To allow record access within the callback function need same index of node-id and itemInfo */
    std::vector<UaNodeId>         vUaNodeId;    // array of node ids as to be used within the opcua library
    std::vector<OPCUA_ItemINFO *> vUaItemInfo;  // array of record data including the link with the node description
private:
    int debug;
    int autoConnect;
    UaSession* m_pSession;
    DevUaSubscription* m_pDevUaSubscription;
    UaClient::ServerStatus serverConnectionStatus;
    bool initialSubscriptionOver;
    autoSessionConnect *autoConnector;
    epicsTimerQueueActive &queue;
};

// Timer to retry connecting the session when the server is down at IOC startup
class autoSessionConnect : public epicsTimerNotify {
public:
    autoSessionConnect(DevUaClient *client, const double delay, epicsTimerQueueActive &queue)
        : timer(queue.createTimer())
        , client(client)
        , delay(delay)
    {}
    virtual ~autoSessionConnect() { timer.destroy(); }
    void start() { timer.start(*this, delay); }
    virtual expireStatus expire(const epicsTime &/*currentTime*/) {
        UaStatus result = client->connect();
        if (result.isBad()) {
            return expireStatus(restart, delay);
        } else {
            return expireStatus(noRestart);
        }
    }
private:
    epicsTimer &timer;
    DevUaClient *client;
    const double delay;
};

void printVal(UaVariant &val,OpcUa_UInt32 IdxUaItemInfo);
void print_OpcUa_DataValue(_OpcUa_DataValue *d);

// Configurable default for auto connection attempt interval

static double drvOpcua_AutoConnectInterval = 10.0;

extern "C" {
    epicsExportAddress(double, drvOpcua_AutoConnectInterval);
}

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

DevUaClient::DevUaClient(int autoCon=1,int debug=0)
    : debug(debug)
    , serverConnectionStatus(UaClient::Disconnected)
    , initialSubscriptionOver(false)
    , queue (epicsTimerQueueActive::allocate(true))
{
    m_pSession            = new UaSession();
    m_pDevUaSubscription  = new DevUaSubscription(getDebug());
    autoConnect = autoCon;
    if(autoConnect)
        autoConnector     = new autoSessionConnect(this, drvOpcua_AutoConnectInterval, queue);
}

DevUaClient::~DevUaClient()
{
    delete m_pDevUaSubscription;
    if (m_pSession)
    {
        if (m_pSession->isConnected())
        {
            ServiceSettings serviceSettings;
            m_pSession->disconnect(serviceSettings, OpcUa_True);
        }
        delete m_pSession;
        m_pSession = NULL;
    }
    queue.release();
    if(autoConnect)
        delete autoConnector;
}

void DevUaClient::connectionStatusChanged(
    OpcUa_UInt32             clientConnectionId,
    UaClient::ServerStatus   serverStatus)
{
    OpcUa_ReferenceParameter(clientConnectionId);
    char timeBuffer[30];

    if(debug)
        errlogPrintf("%s opcUaClient: Connection status changed to %d (%s)\n",
                 getTime(timeBuffer),
                 serverStatus,
                 serverStatusStrings(serverStatus));

    switch (serverStatus)
    {
    case UaClient::ConnectionErrorApiReconnect:
    case UaClient::ServerShutdown:
        this->setBadQuality();
        this->unsubscribe();
        break;
    case UaClient::ConnectionWarningWatchdogTimeout:
        this->setBadQuality();
        break;
    case UaClient::Connected:
        if(serverConnectionStatus == UaClient::ConnectionErrorApiReconnect
                || serverConnectionStatus == UaClient::NewSessionCreated
                || (serverConnectionStatus == UaClient::Disconnected && initialSubscriptionOver)) {
            this->subscribe();
            OpcUaSetupMonitors();
        }
        break;
    case UaClient::Disconnected:
    case UaClient::NewSessionCreated:
        break;
    }
    serverConnectionStatus = serverStatus;
}

// Set uaItem->stat = 1 if connectionStatusChanged() to bad connection
void DevUaClient::setBadQuality()
{
    epicsTimeStamp	 now;
    epicsTimeGetCurrent(&now);

    for(OpcUa_UInt32 bpItem=0;bpItem<vUaItemInfo.size();bpItem++) {
        OPCUA_ItemINFO *uaItem = vUaItemInfo[bpItem];
        uaItem->prec->time = now;
        uaItem->flagSuppressWrite = 1;
        uaItem->stat = 1;
        if(uaItem->inpDataType) // is OUT Record
            callbackRequest(&(uaItem->callback));
        else
            scanIoRequest( uaItem->ioscanpvt );
    }
}

// add OPCUA_ItemINFO to vUaItemInfo. Setup nodes is done by getNodes()
void DevUaClient::addOPCUA_Item(OPCUA_ItemINFO *h)
{
    vUaItemInfo.push_back(h);
    h->itemIdx = vUaItemInfo.size()-1;
    if((h->debug >= 4) || (debug >= 4))
        errlogPrintf("%s\tDevUaClient::addOPCUA_ItemINFO: idx=%d\n", h->prec->name, h->itemIdx);
}

void DevUaClient::setDebug(int d)
{
    m_pDevUaSubscription->debug = d;
    this->debug = d;
}

int DevUaClient::getDebug()
{
    return this->debug;
}


UaStatus DevUaClient::connect()
{
    UaStatus result;

    // Provide information about the client
    SessionConnectInfo sessionConnectInfo;
    sessionConnectInfo.sApplicationName = "HelmholtzgesellschaftBerlin Test Client";
    // Use the host name to generate a unique application URI
    sessionConnectInfo.sApplicationUri  = UaString("urn:%1:HelmholtzgesellschaftBerlin:TestClient").arg(hostName);
    sessionConnectInfo.sProductUri      = "urn:HelmholtzgesellschaftBerlin:TestClient";
    sessionConnectInfo.sSessionName     = sessionConnectInfo.sApplicationUri;

    // Security settings are not initialized - we connect without security for now
    SessionSecurityInfo sessionSecurityInfo;

    if(debug) errlogPrintf("DevUaClient::connect() connecting to '%s'\n", url.toUtf8());
    result = m_pSession->connect(url, sessionConnectInfo, sessionSecurityInfo, this);

    if (result.isBad())
    {
        errlogPrintf("DevUaClient::connect() connection attempt failed with status %#8x (%s)\n",
                     result.statusCode(),
                     result.toString().toUtf8());
        autoConnector->start();
    }

    return result;
}

UaStatus DevUaClient::disconnect()
{
    UaStatus result;

    // Default settings like timeout
    ServiceSettings serviceSettings;
    char buf[30];
    if(debug) errlogPrintf("%s Disconnecting the session\n",getTime(buf));
    result = m_pSession->disconnect(serviceSettings,OpcUa_True);

    if (result.isBad())
    {
        errlogPrintf("%s DevUaClient::disconnect failed with status %#8x (%s)\n",
                     getTime(buf),result.statusCode(),
                     result.toString().toUtf8());
    }

    return result;
}

UaStatus DevUaClient::subscribe()
{
    return m_pDevUaSubscription->createSubscription(m_pSession);
}

UaStatus DevUaClient::unsubscribe()
{
    return m_pDevUaSubscription->deleteSubscription();
}

void split(std::vector<std::string> &sOut,std::string &str, const char delimiter) {
    std::stringstream ss(str); // Turn the string into a stream.
    std::string tok;

    while(getline(ss, tok, delimiter)) {
        sOut.push_back(tok);
    }

    return;
}

long DevUaClient::getBrowsePathItem(OpcUa_BrowsePath &browsePaths,std::string &ItemPath,const char nameSpaceDelim,const char pathDelimiter)
{
    UaRelativePathElements  pathElements;
    std::vector<std::string> devpath;
    std::ostringstream ss;
    boost::regex rex;
    boost::cmatch matches;
    ss <<"([0-9]+)"<< nameSpaceDelim <<"(.*)";
    rex = ss.str();  // ="([a-z0-9_-]+)([,:])(.*)";

    browsePaths.StartingNode.Identifier.Numeric = OpcUaId_ObjectsFolder;

    split(devpath,ItemPath,pathDelimiter);
    pathElements.create(devpath.size());
    
    OpcUa_UInt16    nsIdx=0;
    for(OpcUa_UInt32 i=0; i<devpath.size(); i++) {
        std::string partPath = devpath[i];
        std::string nsStr;
        if (! boost::regex_match(partPath.c_str(), matches, rex) || (matches.size() != 3)) {
            if(!nsIdx)    // first element must set namespace!
                return 1;
            //errlogPrintf("      p='%s'\n",partPath.c_str());
        }
        else {
            char         *endptr;
            nsStr = matches[1];
            partPath = matches[2];
            nsIdx = strtol(nsStr.c_str(),&endptr,10); // regexp guarantees number
            //errlogPrintf("  n=%d,p='%s'\n",nsIdx,partPath.c_str());
            if(nsIdx == 0)     // namespace of 0 is illegal!
                return 1;
        }
        pathElements[i].IncludeSubtypes = OpcUa_True;
        pathElements[i].IsInverse       = OpcUa_False;
        pathElements[i].ReferenceTypeId.Identifier.Numeric = OpcUaId_HierarchicalReferences;
        OpcUa_String_AttachCopy(&pathElements[i].TargetName.Name, partPath.c_str());
        pathElements[i].TargetName.NamespaceIndex = nsIdx;
    }
    browsePaths.RelativePath.NoOfElements = pathElements.length();
    browsePaths.RelativePath.Elements = pathElements.detach();
    return 0;
}

/* clear vUaNodeId and recreate all nodes from uaItem->ItemPath data.
 *    vUaItemInfo:  input link is either
 *    NODE_ID    or      BROWSEPATH
 *       |                   |
 *    getNodeId          getBrowsePathItem()
 *       |                   |
 *       |               translateBrowsePathsToNodeIds()
 *       |                   |
 *    vUaNodeId holds all nodes.
 * Index of vUaItemInfo has to match index of vUaNodeId to get record
 * access in DevUaSubscription::dataChange callback!
 */
long DevUaClient::getNodes()
{
    long ret=0;
    long isIdType = 1;  /* flag to make shure consistency of itemPath types id==1 or browsepath==0. First item defines type!  */
    OpcUa_UInt32    i;
    OpcUa_UInt32    nrOfItems = vUaItemInfo.size();
    OpcUa_UInt32    nrOfBrowsePathItems=0;
    std::vector<UaNodeId>     vReadNodeIds;
    char delim;
    char isNodeIdDelim = ',';
    char isNameSpaceDelim = ':';
    char pathDelim = '.';
    UaStatus status;
    UaDiagnosticInfos       diagnosticInfos;
    ServiceSettings         serviceSettings;
    UaBrowsePathResults     browsePathResults;
    UaBrowsePaths           browsePaths;

    std::ostringstream ss;
    boost::regex rex;
    boost::cmatch matches;

    ss <<"([a-z0-9_-]+)(["<< isNodeIdDelim << isNameSpaceDelim<<"])(.*)";
    rex = ss.str();  // ="([a-z0-9_-]+)([,:])(.*)";
    vUaNodeId.clear();

    // Defer initialization if server is down when the IOC boots
    if (!m_pSession->isConnected()) {
         errlogPrintf("DevUaClient::getNodes() Session not connected - deferring initialisation\n");
         initialSubscriptionOver = true;
         return 1;
    }

    browsePaths.create(nrOfItems);
    for(i=0;i<nrOfItems;i++) {
        OPCUA_ItemINFO        *uaItem = vUaItemInfo[i];
        std::string ItemPath = uaItem->ItemPath;
        int  ns;    // namespace
        UaNodeId    tempNode;
        if (! boost::regex_match( uaItem->ItemPath, matches, rex) || (matches.size() != 4)) {
            errlogPrintf("%s getNodes() SKIP for bad link. Can't parse '%s'\n",uaItem->prec->name,ItemPath.c_str());
            ret=1;
            continue;
        }
        delim = ((std::string)matches[2]).c_str()[0];
        std::string path = matches[3];

        int isStr=0;
        try {
            ns = boost::lexical_cast<int>(matches[1]);
        }
        catch (const boost::bad_lexical_cast &exc) {
            isStr=1;
        }
        if( isStr ) {      // later versions: string tag to specify a subscription group
            errlogPrintf("%s getNodes() SKIP for bad link. Illegal string type namespace tag in '%s'\n",uaItem->prec->name,ItemPath.c_str());
            ret=1;
            continue;
        }

        //errlogPrintf("%20s:ns=%d, delim='%c', path='%s'\n",uaItem->prec->name,ns,delim,path.c_str());
        if(delim == isNameSpaceDelim) {
            if(!i)  // set for first element
                isIdType = 0;
            if (isIdType != 0){
                if(debug) errlogPrintf("%s SKIP for bad link: Illegal Browsepath link in ID links\n",uaItem->prec->name);
                ret = 1;
                continue;
            }
           if(getBrowsePathItem( browsePaths[nrOfBrowsePathItems],ItemPath,isNameSpaceDelim,pathDelim)){  // ItemPath: 'namespace:path' may include other namespaces within the path
                if(debug) errlogPrintf("%s SKIP for bad link: Illegal or Missing namespace in '%s'\n",uaItem->prec->name,ItemPath.c_str());
                ret = 1;
                continue;
            }
            nrOfBrowsePathItems++;
        }
        else if(delim == isNodeIdDelim) {
            if (isIdType != 1){
                if(debug) errlogPrintf("%s SKIP for bad link: Illegal ID link in Browsepath links\n",uaItem->prec->name);
                ret = 1;
                continue;
            }
           // test identifier for number
            OpcUa_UInt32 itemId;
            char         *endptr;

            itemId = (OpcUa_UInt32) strtol(path.c_str(), &endptr, 10);
            if(endptr == NULL) { // numerical id
                tempNode.setNodeId( itemId, ns);
            }
            else {                 // string id
                tempNode.setNodeId(UaString(path.c_str()), ns);
            }
            if(debug>2) errlogPrintf("%3u %s\tNODE: '%s'\n",i,uaItem->prec->name,tempNode.toString().toUtf8());
            vUaNodeId.push_back(tempNode);
            vReadNodeIds.push_back(tempNode);
        }
        else {
            errlogPrintf("%s SKIP for bad link: '%s' unknown delimiter\n",uaItem->prec->name,ItemPath.c_str());
            ret = 1;
            continue;
        }
    }
    if(ret) /* if there are illegal links: stop here! May be improved by del item in vUaItemInfo, but need same index for vUaItemInfo and vUaNodeId */
        return ret;

    if(nrOfBrowsePathItems) {
        browsePaths.resize(nrOfBrowsePathItems);
        status = m_pSession->translateBrowsePathsToNodeIds(
            serviceSettings, // Use default settings
            browsePaths,
            browsePathResults,
            diagnosticInfos);

        if(debug>=2) errlogPrintf("translateBrowsePathsToNodeIds stat=%d (%s). nrOfItems:%d\n",status.statusCode(),status.toString().toUtf8(),browsePathResults.length());
        for(i=0; i<browsePathResults.length(); i++) {
            UaNodeId tempNode;
            if ( OpcUa_IsGood(browsePathResults[i].StatusCode) ) {
                tempNode = UaNodeId(browsePathResults[i].Targets[0].TargetId.NodeId);
                vUaNodeId.push_back(tempNode);
            }
            else {
                tempNode = UaNodeId();
                vUaNodeId.push_back(tempNode);
            }
            if(debug>=2) errlogPrintf("Node: idx=%d node=%s\n",i,tempNode.toString().toUtf8());
        }

    }
    return ret;
}

UaStatus DevUaClient::createMonitoredItems()
{
    return m_pDevUaSubscription->createMonitoredItems(vUaNodeId,&vUaItemInfo);
}

UaStatus DevUaClient::writeFunc(ServiceSettings &serviceSettings,UaWriteValues &nodesToWrite,UaStatusCodeArray &results,UaDiagnosticInfos &diagnosticInfos)
{
    // Writes variable value synchronous to OPC server
    return m_pSession->write(serviceSettings,nodesToWrite,results,diagnosticInfos);

/*    // Writes variable values asynchronous to OPC server
    OpcUa_UInt32         transactionId=0;
    m_pSession->beginWrite(serviceSettings,nodesToWrite,transactionId);
*/
}

void DevUaClient::writeComplete( OpcUa_UInt32 transactionId,const UaStatus& result,const UaStatusCodeArray& results,const UaDiagnosticInfos& diagnosticInfos)
{
    if(result.isBad() && debug) {
        errlogPrintf("Bad Write Result: ");
        for(unsigned int i=0;i<results.length();i++) {
            errlogPrintf("%s \n",result.isBad()? result.toString().toUtf8():"ok");
    }
}
}

UaStatus DevUaClient::readFunc(UaDataValues &values,ServiceSettings &serviceSettings,UaDiagnosticInfos &diagnosticInfos, int attribute)
{
    UaStatus          result;
    UaReadValueIds nodeToRead;
    OpcUa_UInt32        i,j;

    if(debug>=2) errlogPrintf("CALL DevUaClient::readFunc()\n");
    nodeToRead.create(pMyClient->vUaNodeId.size());
    for (i=0,j=0; i <pMyClient->vUaNodeId.size(); i++ )
    {
        if ( !vUaNodeId[i].isNull() ) {
            nodeToRead[j].AttributeId = attribute;
            (pMyClient->vUaNodeId[i]).copyTo(&(nodeToRead[j].NodeId)) ;
            j++;
        }
        else if (debug){
            errlogPrintf("%s DevUaClient::readValues: Skip illegal node: \n",vUaItemInfo[i]->prec->name);
        }
    }
    nodeToRead.resize(j);
    result = m_pSession->read(
        serviceSettings,
        0,
        OpcUa_TimestampsToReturn_Both,
        nodeToRead,
        values,
        diagnosticInfos);
    if(result.isBad() && debug) {
        errlogPrintf("FAILED: DevUaClient::readFunc()\n");
        if(diagnosticInfos.noOfStringTable() > 0) {
            for(unsigned int i=0;i<diagnosticInfos.noOfStringTable();i++)
                errlogPrintf("%s",UaString(diagnosticInfos.stringTableAt(i)).toUtf8());
        }
    }
    return result;
}

void DevUaClient::itemStat(int verb)
{
    errlogPrintf("OpcUa driver: Connected items: %lu\n", (unsigned long)vUaItemInfo.size());

    if(verb<1)
        return;

    // For new cases set default to next case, and new on to default, for verb >= maxCase
    switch(verb){
    case 1: errlogPrintf("Signals with connection status BAD only:\n");
    case 2: errlogPrintf("idx record Name           epics Type         opcUa Type      Stat NS:PATH\n");
            break;
    default:errlogPrintf("idx record Name           epics Type         opcUa Type      Stat Sampl QSiz Drop NS:PATH\n");
    }

    for (unsigned int i=0; i< vUaItemInfo.size(); i++) {
        OPCUA_ItemINFO* uaItem = vUaItemInfo[i];
        switch(verb){
        case 1: if(uaItem->stat == 0)  // only the bad
                break;
        case 2: errlogPrintf("%3d %-20s %2d,%-15s %2d:%-15s %2d %s\n",
                    uaItem->itemIdx,uaItem->prec->name,
                    uaItem->recDataType,epicsTypeNames[uaItem->recDataType],
                    uaItem->itemDataType,variantTypeStrings(uaItem->itemDataType),
                    uaItem->stat,uaItem->ItemPath );
                break;
        default:errlogPrintf("%3d %-20s %2d,%-15s %2d:%-15s %2d %5g %4u %4s %s\n",
                    uaItem->itemIdx,uaItem->prec->name,
                    uaItem->recDataType,epicsTypeNames[uaItem->recDataType],
                    uaItem->itemDataType,variantTypeStrings(uaItem->itemDataType),
                    uaItem->stat, uaItem->samplingInterval, uaItem->queueSize,
                    ( uaItem->discardOldest ? "old" : "new" ), uaItem->ItemPath );
        }

    }
}

/* Maximize debug level from driver-debug (active >=1) and record-debug (active >= 2)
 * use in setRecVal to minimize call parameters
 */
inline int maxDebug(int dbg,int recDbg) {
    if(dbg) ++dbg;
    if(recDbg>dbg) return recDbg;
    else return dbg;
}
epicsRegisterFunction(maxDebug);

/* write variant value from opcua read or callback to - whatever is determined in uaItem*/
long setRecVal(const UaVariant &val, OPCUA_ItemINFO* uaItem,int debug)
{
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
                memcpy(uaItem->pRecVal,aByte.data(),sizeof(epicsInt8)*uaItem->arraySize);
                break;
            case epicsInt16T:
                val.toInt16Array( aInt16);
                memcpy(uaItem->pRecVal,aInt16.rawData(),sizeof(epicsInt16)*uaItem->arraySize);
                break;
            case epicsEnum16T:
            case epicsUInt16T:
                val.toUInt16Array( aUInt16);
                memcpy(uaItem->pRecVal,aUInt16.rawData(),sizeof(epicsUInt16)*uaItem->arraySize);
                break;
            case epicsInt32T:
                val.toInt32Array( aInt32);
                memcpy(uaItem->pRecVal,aInt32.rawData(),sizeof(epicsInt32)*uaItem->arraySize);
                break;
            case epicsUInt32T:
                val.toUInt32Array( aUInt32);
                memcpy(uaItem->pRecVal,aUInt32.rawData(),sizeof(epicsUInt32)*uaItem->arraySize);
                break;
            case epicsFloat32T:
                val.toFloatArray( aFloat);
                memcpy(uaItem->pRecVal,aFloat.rawData(),sizeof(epicsFloat32)*uaItem->arraySize);
                break;
            case epicsFloat64T:
                val.toDoubleArray( aDouble);
                memcpy(uaItem->pRecVal,aDouble.rawData(),sizeof(epicsFloat64)*uaItem->arraySize);
                break;
            default:
                if(debug >= 2) errlogPrintf("%s setRecVal(): Can't convert array data type\n",uaItem->prec->name);
                return 1;
            }
        }
        else {
            if(debug >= 2) errlogPrintf("%s setRecVal() Error record arraysize %d < OpcItem Size %d\n", uaItem->prec->name,val.arraySize(),uaItem->arraySize);
            return 1;
        }
    }      // end array
    else { // is no array
        void *toRec; // destination of the data, VAL, RVAL field directly: OUT records ** OR **
                     // internal varVal to be set when processed: IN records.
        epicsType dataType;

        if(uaItem->inpDataType) {   // is OUT Record. TODO: what about records data conversion?
            toRec = uaItem->pInpVal;
            dataType = uaItem->inpDataType;
        }
        else {
            toRec = &(uaItem->varVal); // is IN Record
            dataType = uaItem->recDataType;
        }

        switch(dataType){
        case epicsInt8T:
        case epicsUInt8T:
            val.toByte(*((epicsUInt8*)toRec));
            if(debug >= 3)
                    errlogPrintf("\tepicsInt32 recVal: %d '%c'\n",*((epicsUInt8*)toRec),*((epicsUInt8*)toRec));
            break;
        case epicsInt16T:
            val.toInt16(*((epicsInt16*)toRec));
            if(debug >= 3)
                    errlogPrintf("\tepicsInt32 recVal: %d\n",*((epicsInt16*)toRec));
            break;
        case epicsEnum16T:
        case epicsUInt16T:
            val.toUInt16( *((epicsUInt16*)toRec));
            if(debug >= 3)
                    errlogPrintf("\tepicsInt32 recVal: %u\n",*((epicsUInt16*)toRec));
            break;
         case epicsInt32T:
            val.toInt32( *((epicsInt32*)toRec));
            if(debug >= 3)
                    errlogPrintf("\tepicsInt32 recVal: %d\n",*((epicsInt32*)toRec));
            break;
        case epicsUInt32T:
            val.toUInt32( *((epicsUInt32*)toRec));
            if(debug >= 3)
                    errlogPrintf("\tepicsUInt32 recVal: %u\n",*((epicsUInt32*)toRec));
            break;
        case epicsFloat32T:
            val.toFloat( *((epicsFloat32*)toRec));
            if(debug >= 3)
                    errlogPrintf("\tepicsFloat32 recVal: %f\n",*((epicsFloat32*)toRec));
            break;
        case epicsFloat64T:
            val.toDouble( *((epicsFloat64*)toRec));
            if(debug >= 3)
                    errlogPrintf("\tepicsFloat64 recVal: %lf\n",*((epicsFloat64*)toRec));
            break;
        case epicsOldStringT:
            strncpy((char*)toRec,val.toString().toUtf8(),ANY_VAL_STRING_SIZE);    // string length: see epicsTypes.h
            if(debug >= 3)
                    errlogPrintf("\tepicsOldStringT opcVal: '%s'\n",(char*)toRec);
            break;
        default:
            if(debug>= 2)
                    errlogPrintf("%s setRecVal() Error unsupported recDataType '%s'\n",uaItem->prec->name,epicsTypeNames[uaItem->recDataType]);
            return 1;
        }
    }
    return 0;
}

// return 0:ok, 1:data loss may occur, 2:not supported OPCUA-Type
// supposed EPICS-types: double, int32, uint32. As long as VAL,RVAL fields have no other types!
int checkDataLoss(int opctype, epicsType epicstype, int isWrite)
{

    if(isWrite) { // data loss warning for OUT-records
        switch(opctype){
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
            case epicsUInt32T:
            case epicsFloat64T: return 1;
            default:;
            }
            break;
        case OpcUaType_UInt32:
            switch(epicstype){//REC_Datatype(EPICS_Datatype)
            case epicsInt32T:
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
            switch(opctype){
            case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
            case OpcUaType_SByte:
            case OpcUaType_Byte:
            case OpcUaType_Int16:
            case OpcUaType_UInt16:
            case OpcUaType_Int32: return 0;
            case OpcUaType_UInt32:
            case OpcUaType_Float:
            case OpcUaType_Double:
            case OpcUaType_String: return 1;
            default:
                return 2;
            }
        case epicsUInt32T:
                switch(opctype){
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
                switch(opctype){
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
            if(opctype != OpcUaType_String)
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

/***************** C Wrapper Functions ********************/

/* Client: Read / setup monitors. First setup items by setOPCUA_Item() function */
long OpcReadValues(int verbose,int monitored)
    {
    UaStatus status;
    int debugStat = pMyClient->getDebug();

    ServiceSettings   serviceSettings;
    UaDataValues      values;
    UaDiagnosticInfos diagnosticInfos;

    if(verbose){
        errlogPrintf("OpcReadValues\n");
        pMyClient->setDebug(verbose);
    }
    if(pMyClient->getNodes() ) {
        pMyClient->setDebug(debugStat);
        return 1;
    }
    status = pMyClient->readFunc(values,serviceSettings,diagnosticInfos,OpcUa_Attributes_Value );
    if (status.isGood()) {
        if(verbose) errlogPrintf("READ VALUES success: %i\n",values.length());
        for(OpcUa_UInt32 j=0;j<values.length();j++) {
            OPCUA_ItemINFO* uaItem = pMyClient->vUaItemInfo[j];

            if (OpcUa_IsGood(values[j].StatusCode)) {
                UaVariant val = values[j].Value;
                if( val.isArray()) {
                    printVal(val,j);
                    if(monitored) {
                        errlogPrintf("Monitored Arrays not supported yet");
                        return 1;
                    }
                }
                else {
                    if(monitored) 
                        uaItem->debug=3;
                    uaItem->itemDataType = (int) values[j].Value.Datatype;
                    switch((int)uaItem->itemDataType){
                    case OpcUaType_Boolean: uaItem->recDataType = epicsInt8T;      break;
                    case OpcUaType_SByte:   uaItem->recDataType = epicsInt8T;      break;
                    case OpcUaType_Byte:    uaItem->recDataType = epicsUInt8T;     break;
                    case OpcUaType_Int16:   uaItem->recDataType = epicsInt16T;     break;
                    case OpcUaType_UInt16:  uaItem->recDataType = epicsUInt16T;    break;
                    case OpcUaType_Int32:   uaItem->recDataType = epicsInt32T;     break;
                    case OpcUaType_UInt32:  uaItem->recDataType = epicsUInt32T;    break;
                    case OpcUaType_Float:   uaItem->recDataType = epicsFloat32T;   break;
                    case OpcUaType_Double:  uaItem->recDataType = epicsFloat64T;   break;
                    case OpcUaType_String:  uaItem->recDataType = epicsOldStringT; break;
                    default:
                        errlogPrintf("OpcReadValues(): '%s' unsupported opc data type: '%s'", uaItem->prec->name, variantTypeStrings(uaItem->itemDataType));
                    }
                    setRecVal(val,uaItem,4);
                }
            }
            else {
                errlogPrintf("Read item[%i] failed with status %s\n",j,UaStatus(values[j].StatusCode).toString().toUtf8());
            }
        }
    }
    else {
        // Service call failed
        errlogPrintf("READ VALUES failed with status %s\n", status.toString().toUtf8());
        return 1;
    }
    if(monitored) {
        pMyClient->createMonitoredItems();
    }
    pMyClient->setDebug(debugStat);
    return 0;
}
/* Client: write one value. First setup items by setOPCUA_Item() function */
long OpcWriteValue(int opcUaItemIndex,double val,int verbose)
{
    int debugStat = pMyClient->getDebug();
    UaStatus            status;
    ServiceSettings     serviceSettings;    // Use default settings
    UaVariant         tempValue;
    UaWriteValues nodesToWrite;       // Array of nodes to write
    UaStatusCodeArray   results;            // Returns an array of status codes
    UaDiagnosticInfos   diagnosticInfos;    // Returns an array of diagnostic info
    OPCUA_ItemINFO* uaItem;
    uaItem = (pMyClient->vUaItemInfo).at(opcUaItemIndex);

    if(verbose){
        errlogPrintf("OpcWriteValue(%d,%f)\nTRANSLATEBROWSEPATH\n",opcUaItemIndex,val);
        pMyClient->setDebug(verbose);
    }

    nodesToWrite.create(1);
//from OPCUA_ItemINFO:    UaNodeId temp1Node(uaItem->ItemPath,uaItem->NdIdx);
    UaNodeId tempNode(pMyClient->vUaNodeId[uaItem->itemIdx]);
    tempNode.copyTo(&nodesToWrite[0].NodeId);
    nodesToWrite[0].AttributeId = OpcUa_Attributes_Value;
    tempValue.setDouble(val);
    tempValue.copyTo(&nodesToWrite[0].Value.Value);

    // Writes variable value synchronous to OPC server
    status = pMyClient->writeFunc(serviceSettings,nodesToWrite,results,diagnosticInfos);
    if ( status.isBad() )
    {
        errlogPrintf("** Error: UaSession::write failed [ret=%s] **\n", status.toString().toUtf8());
        pMyClient->setDebug(debugStat);
        return 1;
    }
    pMyClient->setDebug(debugStat);
    return 0;
}
/* iocShell: record write func  */
extern "C" {
epicsRegisterFunction(OpcUaWriteItems);
}
long OpcUaWriteItems(OPCUA_ItemINFO* uaItem)
{
    UaStatus            status=0;
    ServiceSettings     serviceSettings;    // Use default settings
    UaVariant           tempValue;
    UaWriteValues       nodesToWrite;       // Array of nodes to write
    UaStatusCodeArray   results;            // Returns an array of status codes
    UaDiagnosticInfos   diagnosticInfos;    // Returns an array of diagnostic info

    nodesToWrite.create(1);
//from OPCUA_ItemINFO:    UaNodeId temp1Node(uaItem->ItemPath,uaItem->NdIdx);
    UaNodeId tempNode(pMyClient->vUaNodeId[uaItem->itemIdx]);
    tempNode.copyTo(&nodesToWrite[0].NodeId);
    nodesToWrite[0].AttributeId = OpcUa_Attributes_Value;

    switch((int)uaItem->itemDataType){
    case OpcUaType_Boolean:
        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:
        case epicsUInt32T:  if( 0 != *((epicsInt32*)(uaItem)->pRecVal) )
                                tempValue.setBool(true);
                            else
                                tempValue.setBool(false);
                            break;
        case epicsFloat64T:  if( *((epicsFloat64*)(uaItem)->pRecVal) == 0.0 ) /* is this reasonable?? or better don't support double */
                                tempValue.setBool(false);
                            else
                                tempValue.setBool(true);
                            break;

        default: return 1;
        }
        break;
    case OpcUaType_SByte:

        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:   tempValue.setByte(*((epicsInt32*)(uaItem)->pRecVal));break;
        case epicsUInt32T:  tempValue.setByte(*((epicsUInt32*)(uaItem)->pRecVal));break;
        case epicsFloat64T: tempValue.setByte(*((epicsFloat64*)(uaItem)->pRecVal));break;
        default: status = 1;
        }
        break;
    case OpcUaType_Byte:
        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:   tempValue.setByte(*((epicsInt32*)(uaItem)->pRecVal));break;
        case epicsUInt32T:  tempValue.setByte(*((epicsUInt32*)(uaItem)->pRecVal));break;
        case epicsFloat64T: tempValue.setByte(*((epicsFloat64*)(uaItem)->pRecVal));break;
        default: status = 1;
        }
        break;
    case OpcUaType_Int16:
        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:   tempValue.setInt16(*((epicsInt32*)(uaItem)->pRecVal));break;
        case epicsUInt32T:  tempValue.setInt16(*((epicsUInt32*)(uaItem)->pRecVal));break;
        case epicsFloat64T: tempValue.setInt16(*((epicsFloat64*)(uaItem)->pRecVal));break;
        default: status = 1;
        }
        break;
    case OpcUaType_UInt16:
        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:   tempValue.setUInt16( *((epicsInt32*)(uaItem)->pRecVal));break;
        case epicsUInt32T:  tempValue.setUInt16(*((epicsUInt32*)(uaItem)->pRecVal));break;
        case epicsFloat64T: tempValue.setUInt16(*((epicsFloat64*)(uaItem)->pRecVal));break;
        default: status = 1;
        }
        break;
    case OpcUaType_Int32:
        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:   tempValue.setInt32( *((epicsInt32*)(uaItem)->pRecVal));break;
        case epicsUInt32T:  tempValue.setInt32(*((epicsUInt32*)(uaItem)->pRecVal));break;
        case epicsFloat64T: tempValue.setInt32(*((epicsFloat64*)(uaItem)->pRecVal));break;
        default: status = 1;
        }
        break;
    case OpcUaType_UInt32:
        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:   tempValue.setUInt32( *((epicsInt32*)(uaItem)->pRecVal));break;
        case epicsUInt32T:  tempValue.setUInt32(*((epicsUInt32*)(uaItem)->pRecVal));break;
        case epicsFloat64T: tempValue.setUInt32(*((epicsFloat64*)(uaItem)->pRecVal));break;
        default: status = 1;
        }
        break;
    case OpcUaType_Float:
        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:   tempValue.setFloat( *((epicsInt32*)(uaItem)->pRecVal));break;
        case epicsUInt32T:  tempValue.setFloat(*((epicsUInt32*)(uaItem)->pRecVal));break;
        case epicsFloat64T: tempValue.setFloat(*((epicsFloat64*)(uaItem)->pRecVal));break;
        default: status = 1;
        }
        break;
    case OpcUaType_Double:
        switch(uaItem->recDataType){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:   tempValue.setDouble( *((epicsInt32*)(uaItem)->pRecVal));break;
        case epicsUInt32T:  tempValue.setDouble(*((epicsUInt32*)(uaItem)->pRecVal));break;
        case epicsFloat64T: tempValue.setDouble(*((epicsFloat64*)(uaItem)->pRecVal));break;
        default: status = 1;
        }
        break;
    case OpcUaType_String:
        if(uaItem->recDataType == epicsOldStringT) { /* stringin/outRecord definition of 'char val[40]' */
            tempValue.setString(UaString((char*)uaItem->pRecVal));break;
        }
        break;
    default:
        if(pMyClient->getDebug()) errlogPrintf("%20s\tOpcUaWriteItems: unsupported opc data type: '%s'", uaItem->prec->name, variantTypeStrings(uaItem->itemDataType));
    }
    if((status==1) && pMyClient->getDebug()) {
        errlogPrintf("%s\tOpcUaWriteItems: unsupported record data type: '%s'\n",uaItem->prec->name,epicsTypeNames[uaItem->recDataType]);
        return 1;
    }

    tempValue.copyTo(&nodesToWrite[0].Value.Value);

    status = pMyClient->writeFunc(serviceSettings,nodesToWrite,results,diagnosticInfos);
    if ( status.isBad()  ) // write on a read only node is notBad. Can't be checked here!!
    {
        if(pMyClient->getDebug()) errlogPrintf("%s\tOpcUaWriteItems: UaSession::write failed [ret=%s] **\n",uaItem->prec->name,status.toString().toUtf8());
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

    if(pMyClient->getDebug()) errlogPrintf("OpcUaSetupMonitors Browsepath ok len = %d\n",(int)pMyClient->vUaNodeId.size());

    if(pMyClient->getNodes() )
        return 1;
    status = pMyClient->readFunc(values, serviceSettings, diagnosticInfos,OpcUa_Attributes_Value);
    if (status.isBad()) {
        errlogPrintf("OpcUaSetupMonitors: READ VALUES failed with status %s\n", status.toString().toUtf8());
        return -1;
    }
    status = pMyClient->readFunc(attribs, serviceSettings, diagnosticInfos, OpcUa_Attributes_UserAccessLevel);
    if (status.isBad()) {
        errlogPrintf("OpcUaSetupMonitors: READ VALUES failed with status %s\n", status.toString().toUtf8());
        return -1;
    }
    if(pMyClient->getDebug() > 1) errlogPrintf("OpcUaSetupMonitors READ of %d values returned ok\n", values.length());
    for(OpcUa_UInt32 i=0; i<values.length(); i++) {
        OPCUA_ItemINFO* uaItem = pMyClient->vUaItemInfo[i];
        if (OpcUa_IsBad(values[i].StatusCode)) {
            errlogPrintf("%s: Read node '%s' failed with status %s\n",uaItem->prec->name, uaItem->ItemPath,
                         UaStatus(values[i].StatusCode).toString().toUtf8());
        }
        else {
            if(OpcUa_IsBad(attribs[i].StatusCode))
                errlogPrintf("%s: Read attribs' failed with status %s\n",uaItem->prec->name,
                             UaStatus(attribs[i].StatusCode).toString().toUtf8());
            else {
                UaVariant var = attribs[i].Value;
                var.toUInt32(uaItem->userAccLvl);
            }
            if(values[i].Value.ArrayType && !uaItem->isArray) {
                errlogPrintf("%s: Don't Support Array Data\n",uaItem->prec->name);
            }
            else {
                epicsMutexLock(uaItem->flagLock);
                uaItem->itemDataType = (int) values[i].Value.Datatype;
                uaItem->isArray = 0;
                epicsMutexUnlock(uaItem->flagLock);
                if(pMyClient->getDebug() > 3) errlogPrintf("%4d %15s: %p flagSuppressWrite: %d\n",uaItem->itemIdx,uaItem->prec->name,uaItem,uaItem->flagSuppressWrite);

                if(checkDataLoss(uaItem->itemDataType,uaItem->recDataType,(int) uaItem->inpDataType)) {
                    errlogPrintf("%s: write may loose data: %s -> %s\n",uaItem->prec->name,epicsTypeNames[uaItem->recDataType],
                                 variantTypeStrings(uaItem->itemDataType));
                }
                if( (uaItem->userAccLvl & 0x2) == 0 && ((int) uaItem->inpDataType))    // no write access to out record
                    errlogPrintf("%s: no write Access!\n",uaItem->prec->name);
                if( !(uaItem->userAccLvl & 0x1) )                                  // no read access
                    errlogPrintf("%s: no read Access!\n",uaItem->prec->name);
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
void addOPCUA_Item(OPCUA_ItemINFO *h)
{
    pMyClient->addOPCUA_Item(h);
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
                     g_serverUrl.toUtf8(), drvOpcua_AutoConnectInterval);
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

