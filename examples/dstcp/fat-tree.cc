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

NS_LOG_COMPONENT_DEFINE ("FatTree");

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

void install_applications (uint32_t fromPodId, uint32_t serverCount, uint32_t k, NodeContainer servers, double requestRate, struct cdf_table *cdfTable,
        long &flowCount, long &totalFlowSize, double START_TIME, double END_TIME, double FLOW_LAUNCH_END_TIME)
{
    NS_LOG_INFO ("Install applications:");
    for (uint32_t i = 0; i < serverCount * (k / 2); i++)
    {
        uint32_t fromServerIndex = fromPodId * serverCount * (k / 2) + i;

        double startTime = START_TIME + poission_gen_interval (requestRate);
        while (startTime < FLOW_LAUNCH_END_TIME)
        {
            flowCount ++;
            uint16_t port = rand_range (PORT_START, PORT_END);

            uint32_t destServerIndex = fromServerIndex;
            while (destServerIndex >= fromPodId * serverCount * (k / 2)
                    && destServerIndex < (fromPodId + 1) * serverCount * (k / 2))

            {
                destServerIndex = rand_range (0u, serverCount * (k / 2) * k);
            }

	        Ptr<Node> destServer = servers.Get (destServerIndex);
	        Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
	        Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1, 0);
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
            PacketSinkHelper sink ("ns3::TcpSocketFactory",
                    InetSocketAddress (Ipv4Address::GetAny (), port));
            ApplicationContainer sinkApp = sink.Install (servers. Get (destServerIndex));
            sinkApp.Start (Seconds (startTime));
            sinkApp.Stop (Seconds (END_TIME));

            // NS_LOG_INFO ("\tFlow from server: " << fromServerIndex << " to server: "
            //         << destServerIndex << " on port: " << port << " with flow size: "
            //         << flowSize << " [start time: " << startTime <<"]");

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
  // std::cout << "Progress to " << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " seconds simulation time" << std::endl;
  NS_LOG_INFO ("Progress to " << Simulator::Now ().GetSeconds ());
  Simulator::Schedule (interval, &PrintProgress, interval);
}

int main (int argc, char *argv[])
{
  // LogComponentEnable("TcpDcVegas", LOG_LEVEL_DEBUG);
  // std::string outputFilePath = ".";
  LogComponentEnable ("FatTree", LOG_LEVEL_INFO);
  std::string tcpTypeId = "TcpDcVegas";//TcpDcVegas, TcpDstcp, TcpDctcp
  std::string bufferSize = "600p";
  double K1 = 65;
//   double K10 = 65;
  bool enableSwitchEcn = true;
  Time progressInterval = MilliSeconds (10);

  unsigned randomSeed = 1;
  double load = 0.2;

  double START_TIME = 0.0;
  double END_TIME = 0.25;
  double FLOW_LAUNCH_END_TIME = 0.1;
  std::string cdfFileName = "DCTCP_CDF.txt";

  uint32_t k = 4;
  uint32_t serverCount = 4;

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

  Config::SetDefault ("ns3::TcpDstcp::DstcpMinRtt", TimeValue (MicroSeconds(127)));//127us
  Config::SetDefault ("ns3::TcpDstcp::DstcpTdcvOnInit", UintegerValue (19));

  Config::SetDefault ("ns3::TcpDcVegas::DcVegasMinRtt", TimeValue (MicroSeconds(127)));//127us
  Config::SetDefault ("ns3::TcpDcVegas::DcVegasTdcvOnInit", UintegerValue (19));

  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1448));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (10)));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::UseEcn", EnumValue (TcpSocketState::On));
  // if(tcpTypeId.compare("TcpDstcp") == 0 || tcpTypeId.compare("TcpDcVegas") == 0)
    // {
      //TcpDstcp and TcpDcVegas use Instantaneous RTT samples
  Config::SetDefault ("ns3::RttMeanDeviation::Alpha", DoubleValue (1.0));
    // }
  Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (127)));
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

  if (k % 2 != 0)
    {
        NS_LOG_ERROR ("The k should be 2^n");
        k -= (k % 2);
    }

    uint32_t edgeCount = k * (k / 2);//8
    uint32_t aggregationCount = k * (k / 2);//8
    uint32_t coreCount = (k / 2) * (k / 2);//4

    NodeContainer servers;
    NodeContainer edges;
    NodeContainer aggregations;
    NodeContainer cores;

    servers.Create (serverCount * edgeCount);
    edges.Create (edgeCount);
    aggregations.Create (aggregationCount);
    cores.Create (coreCount);

  NS_LOG_INFO ("Install Internet stacks");
  InternetStackHelper stack;
  Ipv4GlobalRoutingHelper globalRoutingHelper;
  stack.SetRoutingHelper(globalRoutingHelper);
  stack.Install (servers);
  stack.Install (edges);
  stack.Install (aggregations);
  stack.Install (cores);
  
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
  std::vector<Ipv4Address> serverAddresses (serverCount * edgeCount);
  
    NS_LOG_INFO ("Connecting servers to edges");
    for (uint32_t i = 0; i < edgeCount; i++)
    {
        ipv4.NewNetwork ();
        for (uint32_t j = 0; j < serverCount; j++)
        {
            uint32_t uServerIndex = i * serverCount + j;

            NodeContainer nodeContainer = NodeContainer (edges.Get (i), servers.Get (uServerIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

            tchRed10.Install (netDeviceContainer);

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

            serverAddresses[uServerIndex] = interfaceContainer.GetAddress (1);

            // NS_LOG_INFO ("Server-" << uServerIndex << " is connected to Edge-" << i
            //         << " (" << netDeviceContainer.Get (1)->GetIfIndex () << "<->"
            //         << netDeviceContainer.Get (0)->GetIfIndex () << ")");
        }
    }

    NS_LOG_INFO ("Connecting edges to aggregations");
    for (uint32_t i = 0; i < edgeCount; i++)
    {
        for (uint32_t j = 0; j < k / 2; j++)
        {
            uint32_t uAggregationIndex = (i / (k / 2)) * (k / 2) + j;

            NodeContainer nodeContainer = NodeContainer (edges.Get (i), aggregations.Get (uAggregationIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

            tchRed10.Install (netDeviceContainer);

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

            // NS_LOG_INFO ("Edge-" << i << " is connected to Aggregation-" << uAggregationIndex
            //         << " (" << netDeviceContainer.Get (0)->GetIfIndex () << "<->"
            //         << netDeviceContainer.Get (1)->GetIfIndex () << ")");
        }
    }

    NS_LOG_INFO ("Connecting aggregations to cores");
    for (uint32_t i = 0; i < aggregationCount; i++)
    {
        for (uint32_t j = 0; j < k /2; j++)
        {
            uint32_t uCoreIndex = (i % (k / 2)) * (k / 2) + j;

            NodeContainer nodeContainer = NodeContainer (aggregations.Get (i), cores.Get (uCoreIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

            tchRed10.Install (netDeviceContainer);

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

            // NS_LOG_INFO ("Aggregation-" << i << " is connected to Core-" << uCoreIndex
            //         << " (" << netDeviceContainer.Get (0)->GetIfIndex () << "<->"
            //         << netDeviceContainer.Get (1)->GetIfIndex () << ")");
        }
    }

  NS_LOG_INFO ("Populate global routing tables");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  double oversubRatio = static_cast<double> (serverCount * (k / 2) * k * 10000000000) / (10000000000 * (k / 2) * aggregationCount);
  NS_LOG_INFO ("Over-subscription ratio: " << oversubRatio);

  NS_LOG_INFO ("Initialize CDF table");
  struct cdf_table* cdfTable = new cdf_table ();
  init_cdf (cdfTable);
  load_cdf (cdfTable, cdfFileName.c_str ());

  NS_LOG_INFO ("Calculating request rate");
  double requestRate = load * 10000000000 / oversubRatio / (8 * avg_cdf (cdfTable));
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

  for (uint32_t fromPodId = 0; fromPodId < k; ++fromPodId)
  {
    install_applications (fromPodId, serverCount, k, servers, requestRate, cdfTable, flowCount, totalFlowSize, START_TIME, END_TIME, FLOW_LAUNCH_END_TIME);
  }

  NS_LOG_INFO ("Total flow: " << flowCount);
  NS_LOG_INFO ("Actual average flow size: " << static_cast<double> (totalFlowSize) / flowCount);
  NS_LOG_INFO ("Enabling flow monitor");

  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll();
  flowMonitor->CheckForLostPackets ();
  std::stringstream flowMonitorFilename;
  flowMonitorFilename << tcpTypeId << "-fattree-k-" << k << "-load-" << load<< "-seed-" << randomSeed << ".xml";

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