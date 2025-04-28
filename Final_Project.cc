/*
 * Copyright (c) 2017 University of Padova
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Davide Magrin <magrinda@dei.unipd.it>
 */

 #include "ns3/basic-energy-source-helper.h"
 #include "ns3/file-helper.h"
 #include "ns3/lora-radio-energy-model-helper.h"
 #include "ns3/building-allocator.h"
 #include "ns3/building-penetration-loss.h"
 #include "ns3/buildings-helper.h"
 #include "ns3/callback.h"
 #include "ns3/command-line.h"
 #include "ns3/constant-position-mobility-model.h"
 #include "ns3/correlated-shadowing-propagation-loss-model.h"
 #include "ns3/double.h"
 #include "ns3/end-device-lora-phy.h"
 #include "ns3/end-device-lorawan-mac.h"
 #include "ns3/forwarder-helper.h"
 #include "ns3/gateway-lora-phy.h"
 #include "ns3/gateway-lorawan-mac.h"
 #include "ns3/log.h"
 #include "ns3/lora-device-address.h"
 #include "ns3/lora-frame-header.h"
 #include "ns3/lora-helper.h"
 #include "ns3/lora-net-device.h"
 #include "ns3/lora-phy.h"
 #include "ns3/lorawan-mac-header.h"
 #include "ns3/mobility-helper.h"
 #include "ns3/network-server-helper.h"
 #include "ns3/node-container.h"
 #include "ns3/periodic-sender-helper.h"
 #include "ns3/pointer.h"
 #include "ns3/position-allocator.h"
 #include "ns3/random-variable-stream.h"
 #include "ns3/simulator.h"
 
 #include <algorithm>
 #include <ctime>
 
 using namespace ns3;
 using namespace lorawan;
 
 NS_LOG_COMPONENT_DEFINE("AlohaThroughput");
 
 // Network settings
 int nDevices = 20;                 //!< Number of end device nodes to create
 int nGateways = 1;                  //!< Number of gateway nodes to create
 double radiusMeters = 1000;         //!< Radius (m) of the deployment
 double simulationTimeSeconds = 100; //!< Scenario duration (s) in simulated time
 
 // Channel model
 bool realisticChannelModel = false; //!< Whether to use a more realistic channel model with
                                     //!< buildings and correlated shadowing
 
 /** Record received pkts by Data Rate (DR) [index 0 -> DR5, index 5 -> DR0]. */
 auto packetsSent = std::vector<int>(6, 0);
 /** Record received pkts by Data Rate (DR) [index 0 -> DR5, index 5 -> DR0]. */
 auto packetsReceived = std::vector<int>(6, 0);
 
 /**
  * Record the beginning of a transmission by an end device.
  *
  * \param packet A pointer to the packet sent.
  * \param senderNodeId Node id of the sender end device.
  */
 void
 OnTransmissionCallback(Ptr<const Packet> packet, uint32_t senderNodeId)
 {
     NS_LOG_FUNCTION(packet << senderNodeId);
     LoraTag tag;
     packet->PeekPacketTag(tag);
     packetsSent.at(tag.GetSpreadingFactor() - 7)++;
     printf("Send: %d, %f\n", tag.GetSpreadingFactor(), tag.GetFrequency());
 }
 
 /**
  * Record the correct reception of a packet by a gateway.
  *
  * \param packet A pointer to the packet received.
  * \param receiverNodeId Node id of the receiver gateway.
  */
 void
 OnPacketReceptionCallback(Ptr<const Packet> packet, uint32_t receiverNodeId)
 {
     NS_LOG_FUNCTION(packet << receiverNodeId);
     LoraTag tag;
     packet->PeekPacketTag(tag);
     packetsReceived.at(tag.GetSpreadingFactor() - 7)++;
     printf("Receive: %d, %f\n", tag.GetSpreadingFactor(), tag.GetFrequency());
 }
 
 int
 main(int argc, char* argv[])
 {

     // Set up logging
     // LogComponentEnable ("LoraChannel", LOG_LEVEL_INFO);
     // LogComponentEnable ("LoraPhy", LOG_LEVEL_ALL);
     // LogComponentEnable ("EndDeviceLoraPhy", LOG_LEVEL_ALL);
     // LogComponentEnable("GatewayLoraPhy", LOG_LEVEL_ALL);
     // LogComponentEnable("SimpleGatewayLoraPhy", LOG_LEVEL_ALL);
     // LogComponentEnable ("LoraInterferenceHelper", LOG_LEVEL_ALL);
     // LogComponentEnable ("LorawanMac", LOG_LEVEL_ALL);
     // LogComponentEnable ("EndDeviceLorawanMac", LOG_LEVEL_ALL);
     // LogComponentEnable ("ClassAEndDeviceLorawanMac", LOG_LEVEL_ALL);
     // LogComponentEnable("GatewayLorawanMac", LOG_LEVEL_ALL);
     // LogComponentEnable ("LogicalLoraChannelHelper", LOG_LEVEL_ALL);
     // LogComponentEnable ("LogicalLoraChannel", LOG_LEVEL_ALL);
     // LogComponentEnable ("LoraHelper", LOG_LEVEL_ALL);
     // LogComponentEnable ("LoraPhyHelper", LOG_LEVEL_ALL);
     // LogComponentEnable ("LorawanMacHelper", LOG_LEVEL_ALL);
     // LogComponentEnable ("OneShotSenderHelper", LOG_LEVEL_ALL);
     // LogComponentEnable ("OneShotSender", LOG_LEVEL_ALL);
     // LogComponentEnable ("LorawanMacHeader", LOG_LEVEL_ALL);
     // LogComponentEnable ("LoraFrameHeader", LOG_LEVEL_ALL);
     // LogComponentEnableAll(LOG_PREFIX_FUNC);
     // LogComponentEnableAll(LOG_PREFIX_NODE);
     // LogComponentEnableAll(LOG_PREFIX_TIME);

     int dataRate = 5;
     CommandLine cmd(__FILE__);
     cmd.AddValue("nDevices", "Number of end devices to include in the simulation", nDevices);
     cmd.AddValue("simulationTime", "Simulation Time (s)", simulationTimeSeconds);
     cmd.AddValue("radius", "Radius (m) of the deployment", radiusMeters);
     cmd.Parse(argc, argv);
 
     // Set up logging
     //LogComponentEnable("AlohaThroughput", LOG_LEVEL_ALL);
 
     // Make all devices use SF7 (i.e., DR5)
     Config::SetDefault ("ns3::EndDeviceLorawanMac::DataRate", UintegerValue (dataRate));
 
     /***********
      *  Setup  *
      ***********/
 
     // Mobility
     MobilityHelper mobility;
     mobility.SetPositionAllocator("ns3::UniformDiscPositionAllocator",
                                   "rho",
                                   DoubleValue(radiusMeters),
                                   "X",
                                   DoubleValue(0.0),
                                   "Y",
                                   DoubleValue(0.0));
     mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
 
     /************************
      *  Create the channel  *
      ************************/
 
     // Create the lora channel object
     Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
     loss->SetPathLossExponent(3.76);
     loss->SetReference(1, 7.7);
 
     if (realisticChannelModel)
     {
         // Create the correlated shadowing component
         Ptr<CorrelatedShadowingPropagationLossModel> shadowing =
             CreateObject<CorrelatedShadowingPropagationLossModel>();
 
         // Aggregate shadowing to the logdistance loss
         loss->SetNext(shadowing);
 
         // Add the effect to the channel propagation loss
         Ptr<BuildingPenetrationLoss> buildingLoss = CreateObject<BuildingPenetrationLoss>();
 
         shadowing->SetNext(buildingLoss);
     }
 
     Ptr<PropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel>();
 
     Ptr<LoraChannel> channel = CreateObject<LoraChannel>(loss, delay);
 
     /************************
      *  Create the helpers  *
      ************************/
 
     // Create the LoraPhyHelper
     LoraPhyHelper phyHelper = LoraPhyHelper();
     phyHelper.SetChannel(channel);
 
     // Create the LorawanMacHelper
     LorawanMacHelper macHelper = LorawanMacHelper();
     macHelper.SetRegion(LorawanMacHelper::EU);
 
     // Create the LoraHelper
     LoraHelper helper = LoraHelper();
     helper.EnablePacketTracking(); // Output filename
 
     /************************
      *  Create End Devices  *
      ************************/
 
     // Create a set of nodes
     NodeContainer endDevices;
     endDevices.Create(nDevices);
 
     // Assign a mobility model to each node
     mobility.Install(endDevices);
 
     // Make it so that nodes are at a certain height > 0
     for (auto j = endDevices.Begin(); j != endDevices.End(); ++j)
     {
         Ptr<MobilityModel> mobility = (*j)->GetObject<MobilityModel>();
         Vector position = mobility->GetPosition();
         position.z = 1.2;
         mobility->SetPosition(position);
     }
 
     // Create the LoraNetDevices of the end devices
     uint8_t nwkId = 54;
     uint32_t nwkAddr = 1864;
     Ptr<LoraDeviceAddressGenerator> addrGen =
         CreateObject<LoraDeviceAddressGenerator>(nwkId, nwkAddr);
 
     // Create the LoraNetDevices of the end devices
     macHelper.SetAddressGenerator(addrGen);
     phyHelper.SetDeviceType(LoraPhyHelper::ED);
     macHelper.SetDeviceType(LorawanMacHelper::ED_A);
     NetDeviceContainer endDevicesNetDevices = helper.Install(phyHelper, macHelper, endDevices);
 
     // Now end devices are connected to the channel
 
     // Connect trace sources
     for (auto j = endDevices.Begin(); j != endDevices.End(); ++j)
     {
         Ptr<Node> node = *j;
         Ptr<LoraNetDevice> loraNetDevice = node->GetDevice(0)->GetObject<LoraNetDevice>();
         Ptr<LoraPhy> phy = loraNetDevice->GetPhy();
     }
 
     /*********************
      *  Create Gateways  *
      *********************/
 
     // Create the gateway nodes (allocate them uniformly on the disc)
     NodeContainer gateways;
     gateways.Create(nGateways);
 
     Ptr<ListPositionAllocator> allocator = CreateObject<ListPositionAllocator>();
     // Make it so that nodes are at a certain height > 0
     allocator->Add(Vector(0.0, 0.0, 15.0));
     mobility.SetPositionAllocator(allocator);
     mobility.Install(gateways);
 
     // Create a netdevice for each gateway
     phyHelper.SetDeviceType(LoraPhyHelper::GW);
     macHelper.SetDeviceType(LorawanMacHelper::GW);
     helper.Install(phyHelper, macHelper, gateways);

     helper.EnablePeriodicDeviceStatusPrinting(endDevices, gateways, "test.txt", Seconds(100));
 
     NS_LOG_DEBUG("Completed configuration");
 
     /*********************************************
      *  Install applications on the end devices  *
      *********************************************/
 
     Time appStopTime = Seconds(simulationTimeSeconds);
     int packetSize = 50;
     PeriodicSenderHelper appHelper = PeriodicSenderHelper();
     appHelper.SetPeriod(Seconds(1));
     appHelper.SetPacketSize(packetSize);
     ApplicationContainer appContainer = appHelper.Install(endDevices);
 
     appContainer.Start(Seconds(0));
     appContainer.Stop(appStopTime);
 
     // Install trace sources
     for (auto node = gateways.Begin(); node != gateways.End(); node++)
     {
         (*node)->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->TraceConnectWithoutContext(
             "ReceivedPacket",
             MakeCallback(OnPacketReceptionCallback));
     }
 
     // Install trace sources
     for (auto node = endDevices.Begin(); node != endDevices.End(); node++)
     {
         (*node)->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->TraceConnectWithoutContext(
             "StartSending",
             MakeCallback(OnTransmissionCallback));
     }

     /************************
     * Install Energy Model *
     ************************/

    BasicEnergySourceHelper basicSourceHelper;
    LoraRadioEnergyModelHelper radioEnergyHelper;

    // configure energy source
    basicSourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(10000)); // Energy in J
    basicSourceHelper.Set("BasicEnergySupplyVoltageV", DoubleValue(3.3));

    radioEnergyHelper.Set("StandbyCurrentA", DoubleValue(0.0014));
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.028));
    radioEnergyHelper.Set("SleepCurrentA", DoubleValue(0.0000015));
    radioEnergyHelper.Set("RxCurrentA", DoubleValue(0.0112));

    radioEnergyHelper.SetTxCurrentModel("ns3::ConstantLoraTxCurrentModel",
                                        "TxCurrent",
                                        DoubleValue(0.028));

    // install source on end devices' nodes
    EnergySourceContainer sources = basicSourceHelper.Install(endDevices);
    Names::Add("/Names/EnergySource", sources.Get(0));

    // install device model
    DeviceEnergyModelContainer deviceModels =
        radioEnergyHelper.Install(endDevicesNetDevices, sources);

    /**************
     * Get output *
     **************/
    FileHelper fileHelper;
    fileHelper.ConfigureFile("battery-level", FileAggregator::SPACE_SEPARATED);
    fileHelper.WriteProbe("ns3::DoubleProbe", "/Names/EnergySource/RemainingEnergy", "Output");

    for (uint32_t i = 0; i < endDevices.GetN(); i++)
    {

        endDevices.Get(i)
            ->GetDevice(0)
            ->GetObject<LoraNetDevice>()
            ->GetMac()
            ->GetObject<EndDeviceLorawanMac>()
            ->SetDataRate(5);
    }
 
     ////////////////
     // Simulation //
     ////////////////
 
     Simulator::Stop(appStopTime);
 
     NS_LOG_INFO("Running simulation...");
     Simulator::Run();
 
     Simulator::Destroy();
 
     /////////////////////////////
     // Print results to stdout //
     /////////////////////////////
     NS_LOG_INFO("Computing performance metrics...");
 
     for (int i = 0; i < 6; i++)
     {
         std::cout << packetsSent.at(i) << " " << packetsReceived.at(i) << std::endl;
     }
 
     return 0;
 }