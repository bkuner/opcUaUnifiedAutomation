/// @brief OPC UA Server main.
/// @license GNU LGPL
///
/// Distributed under the GNU LGPL License
/// (See accompanying file LICENSE or copy at
/// http://www.gnu.org/licenses/lgpl.html)
///
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <algorithm>

#include <thread>         
#include <chrono>   

#include <opc/ua/node.h>
#include <opc/ua/subscription.h>
#include <opc/ua/server/server.h>

using namespace OpcUa;

uint32_t verbose = 1;

class SubClient : public SubscriptionHandler
{
  void DataChange(uint32_t handle, const Node& node, const Variant& val, AttributeId attr) override
  {
    if(verbose) std::cout << "Received DataChange event for Node " << node << std::endl;
  }
};

inline uint8_t operator|(VariableAccessLevel x, VariableAccessLevel y)
{
    return (uint8_t) ((uint8_t)x | (uint8_t)y);
}

void RunServer(int wait,int debug)
{
  //First setup our server
  bool dbg = false;
  if(debug > 1) dbg = true;
  OpcUa::UaServer server(dbg);
  server.SetEndpoint("opc.tcp://hazel.acc.bessy.de:4841/freeopcua/server");
  server.SetServerURI("urn://exampleserver.freeopcua.github.io");
  server.Start();
  
  //then register our server namespace and get its index in server
  uint32_t idx = server.RegisterNamespace("http://examples.freeopcua.github.io");
  
  //Create our address space using different methods
  Node objects = server.GetObjectsNode();
  
  //Add a custom object with specific nodeid
  NodeId nid(99, idx);
  QualifiedName qn("NewObject", idx);
  Node newobject = objects.AddObject(nid, qn);

  //Variables for to be incremented by the server
  std::vector<int> arrVal = {1,2,3,4,5};
  Node myArrVar = newobject.AddVariable(idx, "incArr", Variant(Variant(arrVal)));

  // Variables for the record test, values given by red write operations from the test records    
  bool mBool = true;
  Node tstBool = newobject.AddVariable(idx,"tstBool", Variant(mBool));
  Node tstInt  = newobject.AddVariable(idx, "tstInt",   Variant((int)0));
  Node tstDbl  = newobject.AddVariable(idx, "tstDbl",   Variant(0.0));
  Node tstStr  = newobject.AddVariable(idx, "tstStr",   Variant(std::string("0-0-0-0-0")));
  std::vector<int> tstArray;
  tstArray.resize(1000);
  Node tstArr  = newobject.AddVariable(idx, "tstArr",  Variant(Variant(tstArray)));

  //Uncomment following to subscribe to datachange events inside server
  /*
  SubClient clt;
  std::unique_ptr<Subscription> sub = server.CreateSubscription(100, clt);
  sub->SubscribeDataChange(myvar);
  */

  if(verbose) std::cout << "Ctrl-C to exit" << std::endl;
  DataValue dataVal;
  for (;;)
  {
    for (idx=0;idx<arrVal.size();idx++) {
        int v = arrVal[idx];
        arrVal[idx] = v+1;
    }
    myArrVar.SetValue(Variant(arrVal));

    Variant nodeVal;
    Node nd;

// BEGIN DOESN'T WORK!! Why ever!!
    // Set status for tstInt: 1=Uncertain, 2=bad, else good
    dataVal = tstInt.GetDataValue();
    dataVal.SetSourceTimestamp(DateTime::Current());
    int val = dataVal.Value.As<int>();
    switch( val ) {
    case 1: dataVal.Status = StatusCode::UncertainInitialValue; break;
    case 2: dataVal.Status = StatusCode::BadInternalError; break;
    default:dataVal.Status = StatusCode::Good;
    }
    tstInt.SetAttribute(AttributeId::Value,dataVal);
//    std::cout<<"tstInt:"<<val<<" stat:"<<(unsigned long)dataVal.Status<<std::endl;
// END DOESN'T WORK!! Why ever!!
    
    std::this_thread::sleep_for(std::chrono::milliseconds(wait));
  }

  server.Stop();
}

int main(int argc, char** argv)
{
    int wait = 2000;
    int c;
    const char help[] = "testServer [OPTIONS]\n"
    "-h:   show help\n"
    "-q:   quiet\n"
    "-t N: Update time (1000ms)\n"
    "Test variables:\n"
    "  NewObject.tstBool\n"
    "  NewObject.tstInt\n"
    "  NewObject.tstDbl\n"
    "  NewObject.tstStr\n"
    "  NewObject.tstArr\n"
    "  NewObject.incArr\n";
    
    opterr = 0;


    while ((c = getopt (argc, argv, "hqv:t")) != -1)
    switch (c)
    {
        case 't':
            wait = atoi(optarg);
            break;
        case 'v':
            verbose = atoi(optarg);
            break;
        case 'q': verbose = 0;
            break;
        case 'h':
            printf("%s",help);
            exit(0);
            break;
    }
    if(verbose) std::cout << "Update (ms): "<<wait<<std::endl;
    try
    {
        RunServer(wait,verbose);
    }
    catch (const std::exception& exc)
    {
        if(verbose) std::cout << "Catch:" <<exc.what() << std::endl;
    }
    catch (...)
    {
        if(verbose) std::cout << "Unknown error." << std::endl;
    }

    return 0;
}

