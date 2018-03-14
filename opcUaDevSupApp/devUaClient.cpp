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
#include "drvOpcUa.h"
#include "devUaSubscription.h"
#include "devUaClient.h"
#include <callback.h>

using namespace UaClientSdk;

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

DevUaClient::DevUaClient(int autoCon=1,int debug=0,double opcua_AutoConnectInterval)
    : debug(debug)
    , serverConnectionStatus(UaClient::Disconnected)
    , initialSubscriptionOver(false)
    , queue (epicsTimerQueueActive::allocate(true))
{
    drvOpcua_AutoConnectInterval = opcua_AutoConnectInterval; // Configurable default for auto connection attempt interval
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


UaStatus DevUaClient::writeFunc(OPCUA_ItemINFO *uaItem, UaVariant &tempValue)
{
    ServiceSettings     serviceSettings;    // Use default settings
    UaWriteValues       nodesToWrite;       // Array of nodes to write
    UaStatusCodeArray   results;            // Returns an array of status codes
    UaDiagnosticInfos   diagnosticInfos;    // Returns an array of diagnostic info

    nodesToWrite.create(1);
    if(uaItem->stat != 0)                          // if connected
        return 0x80000000;

    UaNodeId tempNode(vUaNodeId[uaItem->itemIdx]);
    tempNode.copyTo(&nodesToWrite[0].NodeId);
    nodesToWrite[0].AttributeId = OpcUa_Attributes_Value;
    tempValue.copyTo(&nodesToWrite[0].Value.Value);

    // Writes variable value synchronous to OPC server
    //    return m_pSession->write(serviceSettings,nodesToWrite,results,diagnosticInfos);

    // Writes variable values asynchronous to OPC server
    OpcUa_UInt32 transactionId=uaItem->itemIdx;
    return m_pSession->beginWrite(serviceSettings,nodesToWrite,transactionId);
}

void DevUaClient::writeComplete( OpcUa_UInt32 transactionId,const UaStatus& result,const UaStatusCodeArray& results,const UaDiagnosticInfos& diagnosticInfos)
{
    char timeBuffer[30];

    OPCUA_ItemINFO *uaItem = vUaItemInfo[transactionId];

    if(result.isBad() && debug) {
        errlogPrintf("Bad Write Result: ");
        for(unsigned int i=0;i<results.length();i++) {
            errlogPrintf("%s \n",result.isBad()? result.toString().toUtf8():"ok");
        }
    }
    else {
        if(uaItem->prec->tpro >= 2) errlogPrintf("writeComplete %s: %s\n",uaItem->prec->name, getTime(timeBuffer));
//        callbackRequestProcessCallback(&(uaItem->callback), priorityMedium,uaItem->prec);
        callbackRequest(&(uaItem->callback));
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

