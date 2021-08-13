#include <iostream>
#include <iomanip>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"

// The CDF in TrafficGenerator
extern "C"
{
  #include "cdf.h"
}

#define PORT_START 10000
#define PORT_END 50000

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("LeafSpine");

std::ofstream tQueueLength;

double poission_gen_interval(double avg_rate)
{
    if (avg_rate > 0)
       return -logf(1.0 - (double)rand() / RAND_MAX) / avg_rate;
    else
       return 0;
}

template<typename T>
T rand_range (T min, T max)
{
    return min + ((double)max - min) * rand () / RAND_MAX;
}

void install_applications (int fromLeafId, NodeContainer servers, double requestRate, struct cdf_table *cdfTable,
        long &flowCount, long &totalFlowSize, int SERVER_COUNT, int LEAF_COUNT, double START_TIME, double END_TIME, double FLOW_LAUNCH_END_TIME)
{
    NS_LOG_INFO ("Install applications:");
    for (int i = 0; i < SERVER_COUNT; i++)
    {
        int fromServerIndex = fromLeafId * SERVER_COUNT + i;
        double startTime = START_TIME + poission_gen_interval (requestRate);
          
        while (startTime < FLOW_LAUNCH_END_TIME)
        {
          flowCount ++;
        //   double port = rand_range (PORT_START, PORT_END);
          double port = PORT_START + flowCount;

          int destServerIndex = fromServerIndex;
	        while (destServerIndex >= fromLeafId * SERVER_COUNT && destServerIndex < fromLeafId * SERVER_COUNT + SERVER_COUNT)
            {
		        destServerIndex = rand_range (0, SERVER_COUNT * LEAF_COUNT);
            }

          Ptr<Node> destServer = servers.Get (destServerIndex);
	        Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
	        Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1,0);
	        Ipv4Address destAddress = destInterface.GetLocal ();

          BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (destAddress, port));
          uint32_t flowSize = gen_random_cdf (cdfTable);

          totalFlowSize += flowSize;

          source.SetAttribute ("SendSize", UintegerValue (1448));
          source.SetAttribute ("MaxBytes", UintegerValue(flowSize));

          // Install apps
          ApplicationContainer sourceApp = source.Install (servers.Get (fromServerIndex));
          sourceApp.Start (Seconds (startTime));
          sourceApp.Stop (Seconds (END_TIME));

          // Install packet sinks
          PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
          ApplicationContainer sinkApp = sink.Install (servers. Get (destServerIndex));
          sinkApp.Start (Seconds (START_TIME));
          sinkApp.Stop (Seconds (END_TIME));

          // /*
          // NS_LOG_INFO ("\tFlow from server: " << fromServerIndex << " to server: "
          //           << destServerIndex << " on port: " << port << " with flow size: "
          //           << flowSize << " [start time: " << startTime <<"]");
          // */

          startTime += poission_gen_interval (requestRate);
        }
    }
}

// void
// CheckTQueueSize (Ptr<QueueDisc> queue)
// {
//   // 1500 byte packets
//   uint32_t qSize = queue->GetNPackets ();
//   Time backlog = Seconds (static_cast<double> (qSize * 1500 * 8) / 1e10); // 10 Gb/s
//   // report size in units of packets and ms
//   tQueueLength << std::fixed << std::setprecision (3) << Simulator::Now ().GetSeconds () << " " << qSize << " " << backlog.GetMicroSeconds () << std::endl;
//   // check queue size every 1/100 of a second
//   Simulator::Schedule (MilliSeconds (1), &CheckTQueueSize, queue);
// }

void
PrintProgress (Time interval)
{
  std::cout << "Progress to " << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " seconds simulation time" << std::endl;
  Simulator::Schedule (interval, &PrintProgress, interval);
}

int main (int argc, char *argv[])
{
  // LogComponentEnable("TcpDcVegas", LOG_LEVEL_DEBUG);
  // std::string outputFilePath = ".";
  LogComponentEnable ("LeafSpine", LOG_LEVEL_INFO);
  std::string tcpTypeId = "TcpDctcp";//TcpDcVegas, TcpDstcp, TcpDctcp
  std::string bufferSize = "600p";
  double K1 = 65;
//   double K10 = 65;
  bool enableSwitchEcn = true;
  Time progressInterval = MilliSeconds (10);

  unsigned randomSeed = 11;
  double load = 1.0;

  double START_TIME = 0.0;
  double END_TIME = 0.25;
  double FLOW_LAUNCH_END_TIME = 0.1;
  std::string cdfFileName = "DCTCP_CDF.txt";

  int SERVER_COUNT = 8;
  int SPINE_COUNT = 4;
  int LEAF_COUNT = 4;
  int LINK_COUNT = 1;

  CommandLine cmd (__FILE__);
  cmd.AddValue ("tcpTypeId", "ns-3 TCP TypeId", tcpTypeId);
  cmd.AddValue ("enableSwitchEcn", "enable ECN at switches", enableSwitchEcn);
  cmd.AddValue ("randomSeed", "Random seed, 0 for random generated", randomSeed);
  cmd.AddValue ("load", "Load of the network, 0.0 - 1.0", load);
  cmd.Parse (argc, argv);

  NS_LOG_INFO ("tcpTypeId: " << tcpTypeId);
  NS_LOG_INFO ("load: " << load);

  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + tcpTypeId));
  if(tcpTypeId.compare("TcpDcVegas") == 0)
    {
      //TcpDcVegas doesn't use RED queue, INT_MAX makea the threshold useless
      K1 = INT_MAX;
    }
  Config::SetDefault ("ns3::TcpDstcp::DstcpMinRtt", TimeValue (MicroSeconds(85)));//85us
  Config::SetDefault ("ns3::TcpDstcp::DstcpTdcvOnInit", UintegerValue (16));

  Config::SetDefault ("ns3::TcpDcVegas::DcVegasMinRtt", TimeValue (MicroSeconds(85)));//85us
  Config::SetDefault ("ns3::TcpDcVegas::DcVegasTdcvOnInit", UintegerValue (16));

  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1448));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (10)));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::UseEcn", EnumValue (TcpSocketState::On));
  if(tcpTypeId.compare("TcpDstcp") == 0 || tcpTypeId.compare("TcpDcVegas") == 0)
    {
      //TcpDstcp and TcpDcVegas use Instantaneous RTT samples
      Config::SetDefault ("ns3::RttMeanDeviation::Alpha", DoubleValue (1.0));
    }
  // Set default parameters for RED queue disc
  Config::SetDefault ("ns3::RedQueueDisc::UseEcn", BooleanValue (enableSwitchEcn));
  // ARED may be used but the queueing delays will increase; it is disabled
  // here because the SIGCOMM paper did not mention it
  // Config::SetDefault ("ns3::RedQueueDisc::ARED", BooleanValue (true));
  // Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (true));
  Config::SetDefault ("ns3::RedQueueDisc::UseHardDrop", BooleanValue (false));
  Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (1500));
  // Triumph and Scorpion switches used in DCTCP Paper have 4 MB of buffer
  // If every packet is 1500 bytes, 2666 packets can be stored in 4 MB
  Config::SetDefault ("ns3::RedQueueDisc::MaxSize", QueueSizeValue (QueueSize (bufferSize)));
  // DCTCP tracks instantaneous queue length only; so set QW = 1
  Config::SetDefault ("ns3::RedQueueDisc::QW", DoubleValue (1));
  // Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (20));
  // Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (60));

  NodeContainer spines;
  spines.Create (SPINE_COUNT);
  NodeContainer leaves;
  leaves.Create (LEAF_COUNT);
  NodeContainer servers;
  servers.Create (SERVER_COUNT * LEAF_COUNT);

  NS_LOG_INFO ("Install Internet stacks");
  InternetStackHelper stack;
  Ipv4GlobalRoutingHelper globalRoutingHelper;
  stack.SetRoutingHelper(globalRoutingHelper);
  stack.Install(servers);
  stack.Install(spines);
  stack.Install(leaves);
  
  NS_LOG_INFO ("Install channels and assign addresses");
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("10us"));

  TrafficControlHelper tchRed10;
  tchRed10.SetRootQueueDisc ("ns3::RedQueueDisc",
                            "LinkBandwidth", StringValue ("10Gbps"),
                            "LinkDelay", StringValue ("10us"),
                            "MinTh", DoubleValue (K1),
                            "MaxTh", DoubleValue (K1));
//   QueueDiscContainer queueDiscs = tchRed1.Install (ST[SERVER_COUNT-1]);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.0.0", "255.255.255.0");
  std::vector<Ipv4Address> leafNetworks (LEAF_COUNT);
  std::vector<Ipv4Address> serverAddresses (SERVER_COUNT * LEAF_COUNT);
  
  NS_LOG_INFO ("Configuring servers");
    for (int i = 0; i < LEAF_COUNT; i++)
      {
	    Ipv4Address network = ipv4.NewNetwork ();
        leafNetworks[i] = network;

        for (int j = 0; j < SERVER_COUNT; j++)
          {
            int serverIndex = i * SERVER_COUNT + j;
            NodeContainer nodeContainer = NodeContainer (leaves.Get (i), servers.Get (serverIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);
		    //NS_LOG_INFO ("Install RED Queue for leaf: " << i << " and server: " << j);
	        tchRed10.Install (netDeviceContainer);
            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

//            NS_LOG_INFO ("Leaf - " << i << " is connected to Server - " << j << " with address "
//                    << interfaceContainer.GetAddress(0) << " <-> " << interfaceContainer.GetAddress (1)
//                    << " with port " << netDeviceContainer.Get (0)->GetIfIndex () << " <-> " << netDeviceContainer.Get (1)->GetIfIndex ());

            serverAddresses [serverIndex] = interfaceContainer.GetAddress (1);
          }
      }

  NS_LOG_INFO ("Configuring switches");
    for (int i = 0; i < LEAF_COUNT; i++)
      {
        for (int j = 0; j < SPINE_COUNT; j++)
          {
        	for (int l = 0; l < LINK_COUNT; l++)
        	  {
            // TODO
        		ipv4.NewNetwork ();
        		NodeContainer nodeContainer = NodeContainer (leaves.Get (i), spines.Get (j));
        		NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);
        		//NS_LOG_INFO ("Install RED Queue for leaf: " << i << " and spine: " << j);
        		tchRed10.Install (netDeviceContainer);
        		Ipv4InterfaceContainer ipv4InterfaceContainer = ipv4.Assign (netDeviceContainer);
        		//NS_LOG_INFO ("ipv4.Assign");
//        		NS_LOG_INFO ("Leaf - " << i << " is connected to Spine - " << j << " with address "
//                    << ipv4InterfaceContainer.GetAddress(0) << " <-> " << ipv4InterfaceContainer.GetAddress (1)
//                    << " with port " << netDeviceContainer.Get (0)->GetIfIndex () << " <-> " << netDeviceContainer.Get (1)->GetIfIndex ()
//                    << " with data rate " << spineLeafCapacity);
        	  }
          }
      }

  NS_LOG_INFO ("Populate global routing tables");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  double oversubRatio = static_cast<double>(SERVER_COUNT * 10000000000) / (10000000000 * SPINE_COUNT * LINK_COUNT);
  NS_LOG_INFO ("Over-subscription ratio: " << oversubRatio);

  NS_LOG_INFO ("Initialize CDF table");
  struct cdf_table* cdfTable = new cdf_table ();
  init_cdf (cdfTable);
  load_cdf (cdfTable, cdfFileName.c_str ());

  NS_LOG_INFO ("Calculating request rate");
  double requestRate = load * 10000000000 * SERVER_COUNT / oversubRatio / (8 * avg_cdf (cdfTable)) / SERVER_COUNT;
  NS_LOG_INFO ("Average request rate: " << requestRate << " per second");

  NS_LOG_INFO ("Initialize random seed: " << randomSeed);
  if (randomSeed == 0)
  {
    srand ((unsigned)time (NULL));
  }
  else
  {
     srand (randomSeed);
  }

  NS_LOG_INFO ("Create applications");

  long flowCount = 0;
  long totalFlowSize = 0;

  for (int fromLeafId = 0; fromLeafId < LEAF_COUNT; fromLeafId ++)
  {
    install_applications(fromLeafId, servers, requestRate, cdfTable, flowCount, totalFlowSize, SERVER_COUNT, LEAF_COUNT, START_TIME, END_TIME, FLOW_LAUNCH_END_TIME);
  }

  NS_LOG_INFO ("Total flow: " << flowCount);
  NS_LOG_INFO ("Actual average flow size: " << static_cast<double> (totalFlowSize) / flowCount);
  NS_LOG_INFO ("Enabling flow monitor");

  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll();
  flowMonitor->CheckForLostPackets ();
  std::stringstream flowMonitorFilename;
  flowMonitorFilename << tcpTypeId << "-leaf-spine-" << LEAF_COUNT << "X" << SPINE_COUNT << "-load-" << load<< "-seed-" << randomSeed << ".xml";

  NS_LOG_INFO ("Start simulation");
  // AnimationInterface anim("single-rack.xml");
  Simulator::Schedule (progressInterval, &PrintProgress, progressInterval);
  Simulator::Stop (Seconds (END_TIME));

//   tQueueLength.open (tcpTypeId + "-sigle-rack-t-length.dat", std::ios::out);
//   tQueueLength << "#Time(s) qlen(pkts) qlen(us)" << std::endl;
//   Simulator::Schedule (Seconds (START_TIME), &CheckTQueueSize, queueDiscs.Get (1));
    
  Simulator::Run ();
  tQueueLength.close ();
  flowMonitor->SerializeToXmlFile(flowMonitorFilename.str (), true, true);
  Simulator::Destroy ();
  free_cdf (cdfTable);
  NS_LOG_INFO ("Stop simulation");
  return 0;
}