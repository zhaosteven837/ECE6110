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
 int packetDelay = 1;
 int packetSize = 50;
 
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
     cmd.AddValue("packetDelay", "Time (s) between packets", packetDelay);
     cmd.AddValue("packetSize", "Size of packet", packetSize);
     cmd.AddValue("dataRate", "Lorawan data rate (0-5)", dataRate);
     cmd.Parse(argc, argv);
 
     // Set up logging
     //LogComponentEnable("AlohaThroughput", LOG_LEVEL_ALL);
 
     // Make all devices use SF7 (i.e., DR5)
     Config::SetDefault("ns3::EndDeviceLorawanMac::DataRate", UintegerValue(dataRate));
 
     /***********
      *  Setup  *
      ***********/
 
     // Mobility
     MobilityHelper mobility;
     mobility.SetPositionAllocator("ns3::UniformDiscPositionAllocator",
                                   "rho", DoubleValue(radiusMeters),
                                   "X", DoubleValue(0.0),
                                   "Y", DoubleValue(0.0));
     mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
 
     /************************
      *  Create the channel  *
      ************************/
 
     // Create the lora channel object
     Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
     loss->SetPathLossExponent(3.76);
     loss->SetReference(1, 7.7);
 
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

     /***********************************************
      *  Allocate buildings & install building info  *
      ***********************************************/

     // Urban grid parameters
     const double blockLength = 120.0; // Block size for a medium-scale urban block
     const double streetWidth = 25.1;  // Size for a roadway + sidewalk + furnishing

     const double blockSpacing = blockLength + streetWidth;
     
     // Parameters for a 5x5 grid covering the radius space
     uint32_t gridWidth = static_cast<uint32_t>(2 * radiusMeters / blockSpacing) + 1;
     //uint32_t gridHeight = gridWidth;
     double xLength = 2 * radiusMeters;
     double yLength = xLength;
     double deltaX = xLength / (gridWidth - 1);
     double deltaY = deltaX;

     // Create buildings on a grid
     Ptr<GridBuildingAllocator> gridBuilder = CreateObject<GridBuildingAllocator>();
     gridBuilder->SetAttribute("GridWidth", UintegerValue(gridWidth));
     //gridBuilder->SetAttribute("GridHeight", UintegerValue(gridHeight));
     gridBuilder->SetAttribute("MinX", DoubleValue(-radiusMeters));
     gridBuilder->SetAttribute("MinY", DoubleValue(-radiusMeters));
     gridBuilder->SetAttribute("LengthX", DoubleValue(xLength));
     gridBuilder->SetAttribute("LengthY", DoubleValue(yLength));
     gridBuilder->SetAttribute("DeltaX", DoubleValue(deltaX));
     gridBuilder->SetAttribute("DeltaY", DoubleValue(deltaY));
     gridBuilder->SetAttribute("Height", DoubleValue(16.0));

     // Set the interior attributes of the buildings
     gridBuilder->SetBuildingAttribute("NFloors", UintegerValue(5));

     BuildingContainer bContainer = gridBuilder->Create(gridWidth * gridWidth);

     BuildingsHelper::Install(endDevices);
     BuildingsHelper::Install(gateways);

     /*********************************************
      *  Install applications on the end devices  *
      *********************************************/
 
     Time appStopTime = Hours(1);
     PeriodicSenderHelper appHelper = PeriodicSenderHelper();
     appHelper.SetPeriod(Seconds(packetDelay));
     appHelper.SetPacketSize(packetSize);
     ApplicationContainer appContainer = appHelper.Install(endDevices);
 
     appContainer.Start(Seconds(0));
     appContainer.Stop(appStopTime);

     /************************
     * Install Energy Model *
     ************************/

    BasicEnergySourceHelper basicSourceHelper;
    LoraRadioEnergyModelHelper radioEnergyHelper;

    // LAQ4 -- LoRaWAN Air Quality Sensor
    // 4000mAh Li-SOCI2 battery
    // 3.6 Nominal Voltage
    // 9 uA Standby/Idle Current
    // 24 - 150 mA Tx Current
    // Semtech SX1276 LoRa radio - 9.9mA Rx draw
    basicSourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(51840)); // Energy in J
    basicSourceHelper.Set("BasicEnergySupplyVoltageV", DoubleValue(3.6));

    radioEnergyHelper.Set("StandbyCurrentA", DoubleValue(0.000009));
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.0150));
    radioEnergyHelper.Set("SleepCurrentA", DoubleValue(0.000009));
    radioEnergyHelper.Set("RxCurrentA", DoubleValue(0.0112));

    radioEnergyHelper.SetTxCurrentModel("ns3::ConstantLoraTxCurrentModel", // I don't really know what value could go here from the documentation
                                        "TxCurrent",
                                        DoubleValue(0.0150));

    // install source on end devices' nodes
    EnergySourceContainer sources = basicSourceHelper.Install(endDevices);

    // install device model
    DeviceEnergyModelContainer deviceModels =
        radioEnergyHelper.Install(endDevicesNetDevices, sources);

    /**************
     * Change Specific End Device Data Rates *
     **************/
    
    /*for (uint32_t i = 0; i < endDevices.GetN(); i++)
    {

        endDevices.Get(i)
            ->GetDevice(0)
            ->GetObject<LoraNetDevice>()
            ->GetMac()
            ->GetObject<EndDeviceLorawanMac>()
            ->SetDataRate(5);
    }*/

    /**************
     * Get output *
     **************/
 
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
 
     LoraPacketTracker& tracker = helper.GetPacketTracker();
     NS_LOG_INFO("Printing total sent MAC-layer packets and successful MAC-layer packets");
     std::cout << "Energy Remaining: " << deviceModels.Get(0)->GetTotalEnergyConsumption() << std::endl;
     std::cout << "(Packets sent, Packets received): " << tracker.CountMacPacketsGlobally(Seconds(0), appStopTime + Hours(24)) << std::endl;
 
     return 0;
 }