// Default Network topology, 9 nodes in a star
/*
          n2 n3 n4
           \ | /
            \|/
       n1---n0---n5
            /| \
           / | \
          n8 n7 n6
*/
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TcpServer");

void
TcPacketsInQueueTrace (uint32_t oldValue, uint32_t newValue)
{
  std::cout << "TcPacketsInQueue " << oldValue << " to " << newValue << std::endl;
}

void
DevicePacketsInQueueTrace (uint32_t oldValue, uint32_t newValue)
{
  std::cout << "DevicePacketsInQueue " << oldValue << " to " << newValue << std::endl;
}

int 
main (int argc, char *argv[])
{

  // Set up some default values for the simulation.
  Config::SetDefault ("ns3::OnOffApplication::PacketSize", UintegerValue (250));
  Config::SetDefault ("ns3::OnOffApplication::DataRate", StringValue ("5kb/s"));
  uint32_t N = 9; //number of nodes in the star

  // Allow the user to override any of the defaults and the above
  // Config::SetDefault()s at run-time, via command-line arguments
  CommandLine cmd (__FILE__);
  cmd.AddValue ("nNodes", "Number of nodes to place in the star", N);
  cmd.Parse (argc, argv);

  // Here, we will create N nodes in a star.
  NS_LOG_INFO ("Create nodes.");
  NodeContainer serverNode;
  NodeContainer clientNodes;
  serverNode.Create (1);
  clientNodes.Create (N-1);
  NodeContainer allNodes = NodeContainer (serverNode, clientNodes);

  // Install network stacks on the nodes
  InternetStackHelper internet;
  internet.Install (allNodes);

  //Collect an adjacency list of nodes for the p2p topology
  std::vector<NodeContainer> nodeAdjacencyList (N-1);
  for(uint32_t i=0; i<nodeAdjacencyList.size (); ++i)
    {
      nodeAdjacencyList[i] = NodeContainer (serverNode, clientNodes.Get (i));
    }

  // We create the channels first without any IP addressing information
  NS_LOG_INFO ("Create channels.");
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
  p2p.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue ("1p"));
  std::vector<NetDeviceContainer> deviceAdjacencyList (N-1);
  for(uint32_t i=0; i<deviceAdjacencyList.size (); ++i)
    {
      deviceAdjacencyList[i] = p2p.Install (nodeAdjacencyList[i]);
    }


//Cai dat TC layer len netDevice
  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::RedQueueDisc");   
  std::vector<QueueDiscContainer> qdiscs (N-1);
  for(uint32_t i = 0; i<qdiscs.size (); i++)
    {
      qdiscs[i] = tch.Install (deviceAdjacencyList[i]);
    }
  
  //lay node so 1 de phan tich queue
  Ptr<QueueDisc> q = qdiscs[0].Get (1);
  q->TraceConnectWithoutContext ("PacketsInQueue", MakeCallback (&TcPacketsInQueueTrace));
/*  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TrafficControlLayer/RootQueueDiscList/0/SojournTime", MakeCallback (&SojournTimeTrace));
*/
  Ptr<NetDevice> nd = deviceAdjacencyList[0].Get (1);
  Ptr<PointToPointNetDevice> ptpnd = DynamicCast<PointToPointNetDevice> (nd);
  Ptr<Queue<Packet> > queue = ptpnd->GetQueue ();
  queue->TraceConnectWithoutContext ("PacketsInQueue", MakeCallback (&DevicePacketsInQueueTrace));



  // Later, we add IP addresses.
  NS_LOG_INFO ("Assign IP Addresses.");
  Ipv4AddressHelper ipv4;
  std::vector<Ipv4InterfaceContainer> interfaceAdjacencyList (N-1);
  for(uint32_t i=0; i<interfaceAdjacencyList.size (); ++i)
    {
      std::ostringstream subnet;
      subnet<<"10.1."<<i+1<<".0";
      ipv4.SetBase (subnet.str ().c_str (), "255.255.255.0");
      interfaceAdjacencyList[i] = ipv4.Assign (deviceAdjacencyList[i]);
    }

  //Turn on global static routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Create a packet sink on the star "hub" to receive these packets
  uint16_t port = 50000;
  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
  ApplicationContainer sinkApp = sinkHelper.Install (serverNode);
  sinkApp.Start (Seconds (1.0));
  sinkApp.Stop (Seconds (10.0));

  // Create the OnOff applications to send TCP to the server
  uint32_t payloadSize = 1448;
  OnOffHelper clientHelper ("ns3::TcpSocketFactory", Address ());
  clientHelper.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  clientHelper.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  clientHelper.SetAttribute ("PacketSize", UintegerValue (payloadSize));   
  clientHelper.SetAttribute ("DataRate", StringValue ("100Mbps"));                        
  //normally wouldn't need a loop here but the server IP address is different
  //on each p2p subnet
  ApplicationContainer clientApps;
  for(uint32_t i=0; i<clientNodes.GetN (); ++i)
    {
      AddressValue remoteAddress
        (InetSocketAddress (interfaceAdjacencyList[i].GetAddress (0), port));
      clientHelper.SetAttribute ("Remote", remoteAddress);
      clientApps.Add (clientHelper.Install (clientNodes.Get (i)));
    }
  clientApps.Start (Seconds (1.0));
  clientApps.Stop (Seconds (10.0));
  //Flow monitor ho tro thong ke du lieu
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  Simulator::Stop (Seconds (10));
  Simulator::Run ();

  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  std::cout << std::endl << "*** Flow monitor statistics ***" << std::endl;
  std::cout << "  Tx Packets/Bytes:   " << stats[1].txPackets
            << " / " << stats[1].txBytes << std::endl;
  std::cout << "  Offered Load: " << stats[1].txBytes * 8.0 / (stats[1].timeLastTxPacket.GetSeconds () - stats[1].timeFirstTxPacket.GetSeconds ()) / 1000000 << " Mbps" << std::endl;
  std::cout << "  Rx Packets/Bytes:   " << stats[1].rxPackets
            << " / " << stats[1].rxBytes << std::endl;
  uint32_t packetsDroppedByQueueDisc = 0;
  uint64_t bytesDroppedByQueueDisc = 0;
  if (stats[1].packetsDropped.size () > Ipv4FlowProbe::DROP_QUEUE_DISC)
    {
      packetsDroppedByQueueDisc = stats[1].packetsDropped[Ipv4FlowProbe::DROP_QUEUE_DISC];
      bytesDroppedByQueueDisc = stats[1].bytesDropped[Ipv4FlowProbe::DROP_QUEUE_DISC];
    }
  std::cout << "  Packets/Bytes Dropped by Queue Disc:   " << packetsDroppedByQueueDisc
            << " / " << bytesDroppedByQueueDisc << std::endl;
  uint32_t packetsDroppedByNetDevice = 0;
  uint64_t bytesDroppedByNetDevice = 0;
  if (stats[1].packetsDropped.size () > Ipv4FlowProbe::DROP_QUEUE)
    {
      packetsDroppedByNetDevice = stats[1].packetsDropped[Ipv4FlowProbe::DROP_QUEUE];
      bytesDroppedByNetDevice = stats[1].bytesDropped[Ipv4FlowProbe::DROP_QUEUE];
    }
  std::cout << "  Packets/Bytes Dropped by NetDevice:   " << packetsDroppedByNetDevice
            << " / " << bytesDroppedByNetDevice << std::endl;
  std::cout << "  Throughput: " << stats[1].rxBytes * 8.0 / (stats[1].timeLastRxPacket.GetSeconds () - stats[1].timeFirstRxPacket.GetSeconds ()) / 1000000 << " Mbps" << std::endl;
  std::cout << "  Mean delay:   " << stats[1].delaySum.GetSeconds () / stats[1].rxPackets << std::endl;
  std::cout << "  Mean jitter:   " << stats[1].jitterSum.GetSeconds () / (stats[1].rxPackets - 1) << std::endl;
  auto dscpVec = classifier->GetDscpCounts (1);
  for (auto p : dscpVec)
    {
      std::cout << "  DSCP value:   0x" << std::hex << static_cast<uint32_t> (p.first) << std::dec
                << "  count:   "<< p.second << std::endl;
    }

/*
 Ptr<FlowMonitor>flowMonitor;
 FlowMonitorHelper flowHelper;
 flowMonitor = flowHelper.InstallAll();
 Simulator::Stop (Seconds (10.0));
 Simulator::Run ();
 //Du lieu thong ke se duoc luu vao file truong.xml tai vi tri cung file ./waf
 //cd toi ubuntu@ns3:~/ns-allinone-3.35/netanim-3.108$ ./NetAnim 
 //Vao muc stats --> chon mode Flow-monitor (tai phan IP-MAC)-->FlowMon File --> tim va mo file
 //truong.xml
 flowMonitor->SerializeToXmlFile("truong.xml", true, true);
*/
  Simulator::Destroy ();
  
  std::cout << std::endl << "*** Application statistics in hub ***" << std::endl;
  double thr = 0;
  uint64_t totalPacketsThr = DynamicCast<PacketSink> (sinkApp.Get (0))->GetTotalRx ();
  thr = totalPacketsThr * 8 / (10 * 1000000.0); //Mbit/s
  std::cout << "  Rx Bytes: " << totalPacketsThr << std::endl;
  std::cout << "  Average Goodput: " << thr << " Mbit/s" << std::endl;
  std::cout << std::endl << "*** TC Layer statistics ***" << std::endl;
  std::cout << q->GetStats () << std::endl;
  NS_LOG_INFO ("Done.");

  return 0;
}
