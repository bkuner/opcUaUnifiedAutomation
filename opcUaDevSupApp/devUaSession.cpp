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
#include "devUaSession.h"
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

std::map<std::string,DevUaSession*> DevUaSession::sessions;         // sessionTag = DevUaSession*,       set by drvOpcuaSetup()
std::map<std::string,DevUaSession*> DevUaSession::subscr2session;   // subscriptionTag = DevUaSession*, set by drvOpcuaSubscription()

DevUaSession::DevUaSession(std::string &t,int autoCon=1,int debug=0,double opcua_AutoConnectInterval=10)
    : debug(debug), tag(t)
    , serverConnectionStatus(UaClient::Disconnected)
    , initialSubscriptionOver(false)
    , queue (epicsTimerQueueActive::allocate(true))
{
    DevUaSession::sessions.insert(std::make_pair(tag,this) );

    drvOpcua_AutoConnectInterval = opcua_AutoConnectInterval; // Configurable default for auto connection attempt interval
    m_pSession            = new UaSession();

    autoConnect = autoCon;
    if(autoConnect)
        autoConnector     = new autoSessionConnect(this, drvOpcua_AutoConnectInterval, queue);
}

DevUaSession::~DevUaSession()
{
    std::map<std::string,DevUaSubscription*>::iterator it;
    while(it != subscriptions.end())
    {
        //std::cout<<it->first<<" :: "<<it->second<<std::endl;
        it->second->deleteSubscription();
        it++;
    }

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

void DevUaSession::connectionStatusChanged(
    OpcUa_UInt32             clientConnectionId,
    UaClient::ServerStatus   serverStatus)
{
    OpcUa_ReferenceParameter(clientConnectionId);
    char timeBuffer[30];

    if(debug)
        errlogPrintf("%s opcUaSession: Connection status changed to %d (%s)\n",
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
            startSession();
        }
        break;
    case UaClient::Disconnected:
    case UaClient::NewSessionCreated:
        break;
    }
    serverConnectionStatus = serverStatus;
}

// Set uaItem->stat = 1 if connectionStatusChanged() to bad connection
void DevUaSession::setBadQuality()
{
    epicsTimeStamp	 now;
    epicsTimeGetCurrent(&now);

    for(OpcUa_UInt32 bpItem=0;bpItem<vUaItemInfo.size();bpItem++) {
        UaItem *uaItem = vUaItemInfo[bpItem];
        uaItem->prec->time = now;
        uaItem->stat = 1;
        if(uaItem->inpDataType) // is OUT Record
            callbackRequest(&(uaItem->callback));
        else
            scanIoRequest( uaItem->ioscanpvt );
    }
}

void DevUaSession::setDebug(int d)
{
    this->debug = d;
}

int DevUaSession::getDebug()
{
    return this->debug;
}


UaStatus DevUaSession::connect()
{
    UaStatus result;

    // Provide information about the session
    SessionConnectInfo sessionConnectInfo;
    sessionConnectInfo.sApplicationName = "HelmholtzgesellschaftBerlin Test Client";
    // Use the host name to generate a unique application URI
    sessionConnectInfo.sApplicationUri  = UaString("urn:%1:HelmholtzgesellschaftBerlin:TestClient").arg(hostName);
    sessionConnectInfo.sProductUri      = "urn:HelmholtzgesellschaftBerlin:TestClient";
    sessionConnectInfo.sSessionName     = sessionConnectInfo.sApplicationUri;

    // Security settings are not initialized - we connect without security for now
    SessionSecurityInfo sessionSecurityInfo;

    if(debug) errlogPrintf("DevUaSession::connect() connecting to '%s'\n", url.toUtf8());
    result = m_pSession->connect(url, sessionConnectInfo, sessionSecurityInfo, this);

    if (result.isBad())
    {
        errlogPrintf("DevUaSession::connect() connection attempt failed with status %#8x (%s)\n",
                     result.statusCode(),
                     result.toString().toUtf8());
        autoConnector->start();
    }

    return result;
}

UaStatus DevUaSession::disconnect()
{
    UaStatus result;

    // Default settings like timeout
    ServiceSettings serviceSettings;
    char buf[30];
    if(debug) errlogPrintf("%s Disconnecting the session\n",getTime(buf));
    result = m_pSession->disconnect(serviceSettings,OpcUa_True);

    if (result.isBad())
    {
        errlogPrintf("%s DevUasession::disconnect failed with status %#8x (%s)\n",
                     getTime(buf),result.statusCode(),
                     result.toString().toUtf8());
    }

    return result;
}

long DevUaSession::addSubscription(std::string &subscrTag)
{
    if(DevUaSession::subscr2session.insert(std::make_pair(subscrTag,this)).second == false ) {
        errlogPrintf("Error: Subscription tag '%s' allready defined, must be unique!\n",subscrTag.c_str());
        return 1;
    }

    DevUaSubscription *s = new DevUaSubscription(getDebug());
    subscriptions.insert(std::make_pair(subscrTag,s));

    return 0;
}

UaStatus DevUaSession::subscribe()
{
    UaStatus stat = 0;
    std::map<std::string,DevUaSubscription*>::iterator it;

    while(it != subscriptions.end()) {
        stat = it->second->createSubscription(m_pSession);
        if(stat.isBad())
            break;
        it++;
    }
    return stat;
}

UaStatus DevUaSession::unsubscribe()
{
    UaStatus stat = 0;
    std::map<std::string,DevUaSubscription*>::iterator it;

    while(it != subscriptions.end()) {
        stat = it->second->deleteSubscription();
        if(stat.isBad())
            break;
        it++;
    }
    return stat;
}

void split(std::vector<std::string> &sOut,std::string &str, const char delimiter) {
    std::stringstream ss(str); // Turn the string into a stream.
    std::string tok;

    while(getline(ss, tok, delimiter)) {
        sOut.push_back(tok);
    }

    return;
}
long  DevUaSession::startSession(void)
{
    UaStatus status;
    UaDataValues values;
    UaDataValues attribs; // OpcUa_Attributes_UserAccessLevel
    ServiceSettings     serviceSettings;
    UaDiagnosticInfos   diagnosticInfos;

    if(debug) errlogPrintf("%s: startSession of %d items\n",tag.c_str(),(int)vUaItemInfo.size());

    status = readFunc(values, serviceSettings, diagnosticInfos,OpcUa_Attributes_Value);
    if (status.isBad()) {
        errlogPrintf("%s: startSession: READ VALUES failed with status %s\n",tag.c_str(), status.toString().toUtf8());
        return 1;
    }
    status = readFunc(attribs, serviceSettings, diagnosticInfos, OpcUa_Attributes_UserAccessLevel);
    if (status.isBad()) {
        errlogPrintf("%s: startSession: READ VALUES failed with status %s\n",tag.c_str(), status.toString().toUtf8());
        return 1;
    }
    if(debug > 1) errlogPrintf("startSession READ of %d values returned ok\n", values.length());
    for(OpcUa_UInt32 i=0; i<values.length(); i++) {
        UaItem* uaItem = vUaItemInfo[i];
        if (OpcUa_IsBad(values[i].StatusCode)) {
            uaItem->stat = values[i].StatusCode;
            errlogPrintf("%s: Read node %s '%s' failed with status %s\n",uaItem->prec->name,tag.c_str(), uaItem->ItemPath,
                         UaStatus(values[i].StatusCode).toString().toUtf8());
        }
        else {
            if(OpcUa_IsBad(attribs[i].StatusCode)) {
                uaItem->stat = attribs[i].StatusCode;
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
            if(debug > 0) {
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
                if(debug > 3) errlogPrintf("%4d %15s %p\n",uaItem->itemIdx,uaItem->prec->name,uaItem);
            }
        }
    }

    if(false == m_pSession->isConnected() ) {
        errlogPrintf("\nDevUaSubscription::createMonitoredItems Error: session not connected\n");
        return OpcUa_BadInvalidState;
    }

    std::map<std::string,DevUaSubscription*>::iterator subscrIt = subscriptions.begin();
    while(subscrIt != subscriptions.end()) {
        status = subscrIt->second->setupSubscription();
        if(status.isBad())
            break;
        subscrIt++;
    }
    if (status.isBad()) {
        errlogPrintf("startSession: createMonitoredItems() failed with status %s\n", status.toString().toUtf8());
        return 1;
    }
    return 0;
}


long DevUaSession::getBrowsePathItem(OpcUa_BrowsePath &browsePaths,std::string &ItemPath,const char nameSpaceDelim,const char pathDelimiter)
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
long DevUaSession::getNodes()
{
    long ret=0;
    long isIdType = 1;  /* flag to make shure consistency of itemPath types id==1 or browsepath==0. First item defines type!  */
    OpcUa_UInt32    i;
    OpcUa_UInt32    nrOfItems = vUaItemInfo.size();
    OpcUa_UInt32    nrOfBrowsePathItems=0;
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

    // Defer initialization if server is down when the IOC boots
    if (!m_pSession->isConnected()) {
         errlogPrintf("DevUaSession::getNodes() Session not connected - deferring initialisation\n");
         initialSubscriptionOver = true;
         return 1;
    }

    browsePaths.create(nrOfItems);
    for(i=0;i<nrOfItems;i++) {
        UaItem        *uaItem = vUaItemInfo[i];
        std::string ItemPath = uaItem->ItemPath;
        int  ns;    // namespace
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
            OpcUa_UInt32 itemId;git
            char         *endptr;

            itemId = (OpcUa_UInt32) strtol(path.c_str(), &endptr, 10);
            if(endptr == NULL) { // numerical id
                uaItem->nodeId.setNodeId( itemId, ns);
            }
            else {                 // string id
                uaItem->nodeId.setNodeId(UaString(path.c_str()), ns);
            }
            if(debug>2) errlogPrintf("%3u %s\tNODE: '%s'\n",i,uaItem->prec->name,uaItem->nodeId.toString().toUtf8());

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
            UaItem *uaItem = vUaItemInfo[i];

            if ( OpcUa_IsGood(browsePathResults[i].StatusCode) ) {
                uaItem->nodeId = UaNodeId(browsePathResults[i].Targets[0].TargetId.NodeId);
            }
            else {
                uaItem->nodeId = UaNodeId();
            }
            if(debug>=2) errlogPrintf("Node: idx=%d node=%s\n",i,uaItem->nodeId.toString().toUtf8());
        }

    }
    return ret;
}

UaStatus DevUaSession::writeFunc(UaItem *uaItem, UaVariant &tempValue)
{
    ServiceSettings     serviceSettings;    // Use default settings
    UaWriteValues       nodesToWrite;       // Array of nodes to write
    UaStatusCodeArray   results;            // Returns an array of status codes
    UaDiagnosticInfos   diagnosticInfos;    // Returns an array of diagnostic info

    if (!isConnected()) return 1;
    nodesToWrite.create(1);
    if(uaItem->stat != 0)                          // if connected
        return 0x80000000;

    UaNodeId tempNode = uaItem->nodeId;
    tempNode.copyTo(&nodesToWrite[0].NodeId);
    nodesToWrite[0].AttributeId = OpcUa_Attributes_Value;
    tempValue.copyTo(&nodesToWrite[0].Value.Value);

    // Writes variable value synchronous to OPC server
    //    return m_pSession->write(serviceSettings,nodesToWrite,results,diagnosticInfos);

    // Writes variable values asynchronous to OPC server
    OpcUa_UInt32 transactionId=uaItem->itemIdx;
    return m_pSession->beginWrite(serviceSettings,nodesToWrite,transactionId);
}

void DevUaSession::writeComplete( OpcUa_UInt32 transactionId,const UaStatus& result,const UaStatusCodeArray& results,const UaDiagnosticInfos& diagnosticInfos)
{
    char timeBuffer[30];
    OpcUa_UInt32 i;
    UaItem *uaItem = vUaItemInfo[transactionId];

    if(result.isBad() ) {
        errlogPrintf("writeComplete failed! result: %#8x '%s'",UaStatusCode(result).statusCode(),result.toString().toUtf8());
        uaItem->stat = UaStatusCode(result).statusCode();
    }
    else {
        uaItem->stat = 0;
        for (i=0; i<results.length(); i++ ) // length=1, we write single items
        {
            if ( !OpcUa_IsGood(results[i]) )
            {
                errlogPrintf("** writeComplete of failed: %#8x (%s)\n", UaStatusCode(UaStatus(results[i])).statusCode(),UaStatus(results[i]).toString().toUtf8());
                uaItem->stat = UaStatusCode(results [i]).statusCode();
            }
        }

    }
    if(uaItem->debug >= 2) errlogPrintf("writeComplete %s: %s STAT: %#8x (%s)\n",uaItem->prec->name, getTime(timeBuffer), uaItem->stat,UaStatus(uaItem->stat).toString().toUtf8());
    callbackRequest(&(uaItem->callback));
}

UaStatus DevUaSession::readFunc(UaDataValues &values,ServiceSettings &serviceSettings,UaDiagnosticInfos &diagnosticInfos, int attribute)
{
    UaStatus          result;
    UaReadValueIds nodeToRead;
    OpcUa_UInt32        i,j;

    if(debug>=2) errlogPrintf("CALL DevUaSession::readFunc()\n");
    nodeToRead.create(vUaItemInfo.size());
    for (i=0,j=0; i <vUaItemInfo.size(); i++ )
    {
         UaItem *uaItem = vUaItemInfo[i];
         if ( !(uaItem->nodeId).isNull() ) {
            nodeToRead[j].AttributeId = attribute;
             (uaItem->nodeId).copyTo(&(nodeToRead[j].NodeId)) ;
            j++;
        }
        else if (debug){
            errlogPrintf("%s DevUaSession::readValues: Skip illegal node: \n",vUaItemInfo[i]->prec->name);
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
        errlogPrintf("FAILED: DevUaSession::readFunc()\n");
        if(diagnosticInfos.noOfStringTable() > 0) {
            for(unsigned int i=0;i<diagnosticInfos.noOfStringTable();i++)
                errlogPrintf("%s",UaString(diagnosticInfos.stringTableAt(i)).toUtf8());
        }
    }
    return result;
}

DevUaSession* DevUaSession::getSession(std::string &serverTag)
{
    SessionIterator it = DevUaSession::sessions.find(serverTag);
    if(it == DevUaSession::sessions.end()) {
        errlogPrintf("Error: Server tag '%s' not defined\n",serverTag.c_str());
        return NULL;
    }
    return it->second;
}

// The tag my be a session or a subscription tag
DevUaSession* DevUaSession::addUaItem(std::string &tag,UaItem *uaItem)
{
    DevUaSession* pSession = NULL;

    printf("%s: item of %s\n",uaItem->prec->name,tag.c_str());
    SessionIterator it = DevUaSession::sessions.find(tag);
    if(it == DevUaSession::sessions.end()) {
        printf("no session tag\n");
        SessionIterator it = DevUaSession::subscr2session.find(tag);
        if(it != DevUaSession::subscr2session.end()) {  // is a subscription tag
            pSession = it->second;
            std::map<std::string,DevUaSubscription*>::iterator it;
            it = pSession->subscriptions.find(tag);
            if(it != pSession->subscriptions.end()) {
                (it->second)->addMonitoredItem(uaItem);
            }
            else {
                errlogPrintf("Error PV '%s' on Session '%s': Subscription tag '%s' not defined\n",uaItem->getPv(),pSession->tag.c_str(),tag.c_str());
            }
        }
        else {
            errlogPrintf("Error: Server tag '%s' not defined\n",pSession->tag.c_str());
        }
    }
    else {  // is a session tag, means direct access
printf("%s: unsubscribed item of %s\n",uaItem->prec->name,pSession->tag.c_str());
        pSession = it->second;
        pSession->unsubscribedItems.push_back(uaItem);
    }

    if(pSession != NULL) {
        uaItem->itemIdx = pSession->vUaItemInfo.size();
        (pSession->vUaItemInfo).push_back(uaItem);
        uaItem->pSession = pSession;
    }
    return pSession;

}

void DevUaSession::opcuaSetDebug(int verbose)
{
    SessionIterator it = DevUaSession::sessions.begin();
    while(it != DevUaSession::sessions.end()) {
        DevUaSession* pSession = it->second;
        pSession->setDebug(verbose);
        it++;
    }
    return;
}

void DevUaSession::opcuaStat(int verbose)
{
    SessionIterator it = DevUaSession::sessions.begin();
    while(it != DevUaSession::sessions.end()) {
        DevUaSession* pSession = it->second;
        errlogPrintf("Session: '%s'\n",pSession->getTag());
        pSession->itemStat(verbose);
        it++;
        errlogPrintf("    Subsriptions %lu:\n",pSession->subscriptions.size());
        std::map<std::string,DevUaSubscription*>::iterator subIt = pSession->subscriptions.begin();
        while(subIt != pSession->subscriptions.end()) {
            errlogPrintf("\t%s\n",subIt->first.c_str());
            subIt++;
        }
    }
    return;
}

int DevUaSession::opcuaClose(int debug)
{
    UaStatus status;
    if(debug) errlogPrintf("opcUa_close()\n\tunsubscribe\n");

    SessionIterator it = DevUaSession::sessions.begin();
    while(it != DevUaSession::sessions.end()){
        DevUaSession* pSession = it->second;
        status = pSession->unsubscribe();
        if(debug) errlogPrintf("\tdisconnect\n");
        status = pSession->disconnect();

        delete pSession;
        pSession = NULL;
        it++;
    }

    if(debug) errlogPrintf("\tcleanup\n");
    UaPlatformLayer::cleanup();
    return 0;
}

void DevUaSession::itemStat(int verb)
{
    errlogPrintf("OpcUa driver: Connected items: %lu\n", (unsigned long)vUaItemInfo.size());

    if(verb<1)
        return;

    // For new cases set default to next case, and new on to default, for verb >= maxCase
    switch(verb){
    case 1: errlogPrintf("Signals with connection status BAD only:\n");
    case 2: errlogPrintf("idx record Name          Tag        epics Type         opcUa Type      Stat NS:PATH\n");
            break;
    default:errlogPrintf("idx record Name          Tag        epics Type         opcUa Type      Stat Sampl QSiz Drop NS:PATH\n");
    }

    for (unsigned int i=0; i< vUaItemInfo.size(); i++) {
        UaItem* uaItem = vUaItemInfo[i];
        switch(verb){
        case 1: if(uaItem->stat == 0)  // only the bad
                break;
        case 2: errlogPrintf("%3d %-20s '%-10s' %2d,%-15s %2d:%-15s %#8x '%s' %s\n",
                             uaItem->itemIdx,uaItem->prec->name, uaItem->tag.c_str(),
                             uaItem->recDataType,epicsTypeNames[uaItem->recDataType],
                    uaItem->itemDataType,variantTypeStrings(uaItem->itemDataType),
                    UaStatusCode(uaItem->stat).statusCode(),UaStatus(uaItem->stat).toString().toUtf8(),uaItem->ItemPath );
            break;
        default:errlogPrintf("%3d %-20s '%s' %2d,%-15s %2d:%-15s %#8x '%s' %5g %4u %4s %s\n",
                             uaItem->itemIdx,uaItem->prec->name,uaItem->tag.c_str(),
                             uaItem->recDataType,epicsTypeNames[uaItem->recDataType],
                    uaItem->itemDataType,variantTypeStrings(uaItem->itemDataType),
                    UaStatusCode(uaItem->stat).statusCode(),UaStatus(uaItem->stat).toString().toUtf8(), uaItem->samplingInterval,
                    uaItem->queueSize,( uaItem->discardOldest ? "old" : "new" ), uaItem->ItemPath );
        }
    }

}

void DevUaSession::startAllSessions(void){
    SessionIterator it = DevUaSession::sessions.begin();
    while(it != DevUaSession::sessions.end()){
        DevUaSession* pSession = it->second;
        pSession->startSession();
        it++;
    }
}
