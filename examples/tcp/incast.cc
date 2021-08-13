#include <iostream>
#include <iomanip>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("Incast");
std::stringstream filePlotQueue;
void
CheckQueueSize (Ptr<QueueDisc> queue, std::string filePlotQueue)
{
  uint32_t qSize = StaticCast<RedQueueDisc> (queue)->GetNPackets();

  // check queue size every 1/1000 of a second
  Simulator::Schedule (Seconds (0.004), &CheckQueueSize, queue, filePlotQueue);

  std::ofstream fPlotQueue (filePlotQueue.c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}

int main (int argc, char *argv[])
{
  LogComponentEnable("TcpDstcp", LOG_LEVEL_DEBUG);
  // LogComponentEnable("RttMeanDeviation", LOG_LEVEL_DEBUG);
  std::string outputFilePath = ".";
  std::string tcpTypeId = "TcpDstcp";//TcpDcVegas, TcpDstcp, TcpDctcp
  bool enableSwitchEcn = true;

  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + tcpTypeId));

  // Config::SetDefault ("ns3::TcpDstcp::DstcpTdcvOnInit", UintegerValue (7));
  
  Time startTime = Seconds (0);
  Time stopTime = Seconds (1);
  Time clientStartTime = Seconds (0.01);

  Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (MicroSeconds (100)));
  Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (40)));
  Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (false));

  Config::SetDefault ("ns3::RttMeanDeviation::Alpha", DoubleValue (1.0));
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1460));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::UseEcn", EnumValue (TcpSocketState::On));

  Config::SetDefault ("ns3::RedQueueDisc::UseEcn", BooleanValue (enableSwitchEcn));
  Config::SetDefault ("ns3::RedQueueDisc::UseHardDrop", BooleanValue (false));
  Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (1500));
  Config::SetDefault ("ns3::RedQueueDisc::MaxSize", QueueSizeValue (QueueSize ("400p")));
  Config::SetDefault ("ns3::RedQueueDisc::QW", DoubleValue (1));
  Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (20));
  Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (20));

  uint32_t sendNum = 20;
   // Create sendNum, receiver, and switches
  NodeContainer switches;
  switches.Create(1);
  NodeContainer senders;
  senders.Create(sendNum);
  NodeContainer receiver;
  receiver.Create(1);

  // Configure channel attributes
  PointToPointHelper ptpLink;
  ptpLink.SetDeviceAttribute("DataRate", StringValue ("1Gbps"));
  ptpLink.SetChannelAttribute("Delay", StringValue ("10us"));

  PointToPointHelper neckLink;
  neckLink.SetDeviceAttribute("DataRate", StringValue ("1Gbps"));
  neckLink.SetChannelAttribute("Delay", StringValue ("10us"));

  // Install InternetStack
  InternetStackHelper stack;
  stack.InstallAll();

  // Configure TrafficControlHelper
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc ("ns3::RedQueueDisc",
                            "LinkBandwidth", StringValue ("1Gbps"),
                            "LinkDelay", StringValue ("10us"));

  // Configure Ipv4AddressHelper
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.255.255.0");

  // Configure net devices in nodes and connect sendNum with switches
  for(uint32_t i = 0; i<sendNum; i++)
  {
	  NetDeviceContainer devices;
	  devices = ptpLink.Install(senders.Get(i), switches.Get(0));
	  // tchRed.Install(devices);
	  address.NewNetwork();
	  Ipv4InterfaceContainer interfaces = address.Assign(devices);
  }

  // Connect switches with receiver
  NetDeviceContainer devices;
  devices = neckLink.Install(switches.Get(0), receiver.Get(0));

  // Install queueDiscs in switch
  QueueDiscContainer queueDiscs;
  queueDiscs = tchRed.Install(devices);

  address.NewNetwork();
  Ipv4InterfaceContainer interfaces = address.Assign(devices);
  Ipv4InterfaceContainer sink_interfaces;
  sink_interfaces.Add(interfaces.Get(1));

  // Configure routing
  NS_LOG_INFO ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Configure port/address and install application, establish connection
  NS_LOG_INFO ("Build connections");
  uint16_t port = 50000;

  // long flow
  for(uint16_t i=0; i<senders.GetN(); i++)
  {
    BulkSendHelper ftp("ns3::TcpSocketFactory", InetSocketAddress(sink_interfaces.GetAddress(0,0),port));
    ftp.SetAttribute("SendSize", UintegerValue(1460));
    ftp.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer sourceApp = ftp.Install(senders.Get(i));
    sourceApp.Start(startTime);
    sourceApp.Stop(stopTime);
  }

  PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sinkHelper.Install(receiver.Get(0));
  sinkApp.Start(startTime);
  sinkApp.Stop(stopTime);

// Get queue size
  filePlotQueue << "queue-size-" + tcpTypeId +".plotme";
  remove (filePlotQueue.str ().c_str());
  Ptr<QueueDisc> queue = queueDiscs.Get(0);
  Simulator::ScheduleNow (&CheckQueueSize, queue, filePlotQueue.str());

  ptpLink.EnablePcapAll("test", true);
  //neckLink.EnablePcapAll("test", true);
  Simulator::Stop (stopTime);
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}