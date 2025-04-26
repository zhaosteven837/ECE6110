// lora-energy.cc
#include <iostream>
#include <fstream>
#include <cstdlib>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/buildings-module.h"
#include "ns3/propagation-module.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/lorawan-module.h"
#include "ns3/applications-module.h"
#include "ns3/energy-module.h"
#include "ns3/buildings-propagation-loss-model.h"
#include "ns3/oh-buildings-propagation-loss-model.h"

using namespace ns3;
using namespace ns3::lorawan;

NS_LOG_COMPONENT_DEFINE("LoraEnergySimulation");

int main(int argc, char *argv[])
{
    // Parameterised command line
    double txPower; uint32_t sf; double freq; uint32_t runId; double simTime = 3600.0;
    CommandLine cmd;
    cmd.AddValue("txPower", "Transmission power in dBm", txPower);
    cmd.AddValue("sf", "LoRa spreading factor (7-12)", sf);
    cmd.AddValue("freq", "Frequency in Hz", freq);
    cmd.AddValue("runId", "Run identifier", runId);
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.Parse(argc, argv);
    NS_LOG_INFO("Parameters: txPower="<<txPower<<" dBm, sf="<<sf<<", freq="<<freq<<" Hz, runId="<<runId<<", simTime="<<simTime<<"s");

    // Reproducibility
    SeedManager::SetSeed(12345);
    SeedManager::SetRun(runId);

    // Create output directory
    system("mkdir -p results");

    // Create nodes
    NodeContainer endDevices; endDevices.Create(200);
    NodeContainer gateways;   gateways.Create(1);

    // Install internet stack
    InternetStackHelper internet;
    internet.Install(endDevices);
    internet.Install(gateways);

    // Mobility: gateway on rooftop
    MobilityHelper gwMobility;
    Ptr<ListPositionAllocator> gwPos = CreateObject<ListPositionAllocator>();
    gwPos->Add(Vector(400.0, 400.0, 25.0));
    gwMobility.SetPositionAllocator(gwPos);
    gwMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gwMobility.Install(gateways);

    // Mobility: end-devices randomly on sidewalks at z=1.5m
    MobilityHelper devMobility;
    devMobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=800.0]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=800.0]"));
    devMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    devMobility.Install(endDevices);

    // Create ~50 buildings
    Ptr<UniformRandomVariable> xVar = CreateObject<UniformRandomVariable>();
    Ptr<UniformRandomVariable> yVar = CreateObject<UniformRandomVariable>();
    xVar->SetAttribute("Min", DoubleValue(0.0)); xVar->SetAttribute("Max", DoubleValue(800.0));
    yVar->SetAttribute("Min", DoubleValue(0.0)); yVar->SetAttribute("Max", DoubleValue(800.0));
    for (uint32_t i = 0; i < 50; ++i) {
        double x = xVar->GetValue(), y = yVar->GetValue();
        Ptr<Building> b = CreateObject<Building>();
        b->SetBoundaries(Box(x, x+50.0, y, y+50.0, 0.0, 5*3.0));
        b->SetNFloors(5);
        b->SetAttribute("WallLoss", DoubleValue(15.0));
        BuildingList::Add(b);
    }
    BuildingsHelper::Install(endDevices);
    BuildingsHelper::Install(gateways);

    // Propagation: LogDistance + OH building shadowing
    Ptr<LogDistancePropagationLossModel> logDist = CreateObject<LogDistancePropagationLossModel>();
    logDist->SetPathLossExponent(3.0);
    Ptr<OhBuildingsPropagationLossModel> buildingLoss = CreateObject<OhBuildingsPropagationLossModel>();
    buildingLoss->SetAttribute("ShadowingSigmaIndoor",  DoubleValue(7.0));
    buildingLoss->SetAttribute("ShadowingSigmaOutdoor", DoubleValue(7.0));
    logDist->SetNext(buildingLoss);
    Ptr<PropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel>();
    Ptr<LoraChannel> channel = CreateObject<LoraChannel>(logDist, delay);

    // LoRaWAN PHY/MAC helpers
    LoraPhyHelper phyHelper;
    phyHelper.SetChannel(channel);
    phyHelper.SetDeviceType(LoraPhyHelper::ED);

    LorawanMacHelper macHelper;
    macHelper.SetDeviceType(LorawanMacHelper::ED_A);
    macHelper.SetRegion(LorawanMacHelper::EU);

    LoraHelper loraHelper;

    // Install end-devices
    NetDeviceContainer endDevDevices = loraHelper.Install(phyHelper, macHelper, endDevices);

    // Install gateway
    phyHelper.SetDeviceType(LoraPhyHelper::GW);
    macHelper.SetDeviceType(LorawanMacHelper::GW);
    NetDeviceContainer gwDevices = loraHelper.Install(phyHelper, macHelper, gateways);

    // Configure PHY attributes
    phyHelper.Set("TxPower",        DoubleValue(txPower));
    phyHelper.Set("FrequencyMHz",   DoubleValue(freq / 1e6));
    phyHelper.Set("SpreadingFactor", UintegerValue(sf));

    // Energy model
    BasicEnergySourceHelper sourceHelper;
    sourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(3 * 3600 * 3.0));
    ns3::energy::EnergySourceContainer sources = sourceHelper.Install(endDevices);

    LoraRadioEnergyModelHelper radioHelper;
    radioHelper.Set("TxCurrentA",   DoubleValue(0.024));
    radioHelper.Set("RxCurrentA",   DoubleValue(0.0108));
    radioHelper.Set("IdleCurrentA", DoubleValue(0.0015));
    radioHelper.Set("SleepCurrentA",DoubleValue(0.000001));
    ns3::energy::DeviceEnergyModelContainer deviceModels =
        radioHelper.Install(endDevDevices, sources);

    // IP addressing (assign gateway first so its address is known)
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer gwIfaces = ipv4.Assign(gwDevices);
    Ipv4InterfaceContainer devIfaces = ipv4.Assign(endDevDevices);

    // Applications: UDP sink on gateway
    uint16_t port = 12345;
    Address sinkAddr(InetSocketAddress(gwIfaces.GetAddress(0), port));
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", sinkAddr);
    ApplicationContainer sinkApp = sinkHelper.Install(gateways);
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simTime));

    // OnOff traffic from end-devices
    OnOffHelper onOff("ns3::UdpSocketFactory", sinkAddr);
    onOff.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));
    onOff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=9.999]"));
    onOff.SetAttribute("PacketSize", UintegerValue(50));
    onOff.SetAttribute("DataRate", DataRateValue(DataRate("400kbps")));
    ApplicationContainer apps = onOff.Install(endDevices);
    apps.Start(Seconds(0.0));
    apps.Stop(Seconds(simTime));

    // ASCII tracing (packet-level)
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> phyStream = ascii.CreateFileStream("results/trace_phy.csv");
    Ptr<OutputStreamWrapper> macStream = ascii.CreateFileStream("results/trace_mac.csv");
    loraHelper.EnablePacketTracking(); // connects internal traces

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    // Compute metrics
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp.Get(0));
    double bytesReceived = sink->GetTotalRx();
    double throughput    = bytesReceived * 8.0 / simTime;

    double energyConsumed = 0.0;
    for (uint32_t i = 0; i < sources.GetN(); ++i) {
        Ptr<energy::BasicEnergySource> bs = DynamicCast<energy::BasicEnergySource>(sources.Get(i));
        energyConsumed += bs->GetInitialEnergy() - bs->GetRemainingEnergy();
    }
    double efficiency = throughput / energyConsumed;

    // Write summary
    std::ofstream summary("results/summary.csv", std::ios::app);
    summary << txPower << "," << sf << "," << freq << ","
            << throughput << "," << efficiency << "," << runId << "\n";
    summary.close();

    return 0;
}
