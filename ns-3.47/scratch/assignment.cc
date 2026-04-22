// Roll number: 220003022; WiFi: 7, LR-WPAN: 11; Initial channel: 12; New channel: 25

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/wifi-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("InterferenceAwareChannelSwitching");

static NetDeviceContainer g_lrwpanDevices;

static bool g_wifiBusy = false;
static bool g_switched = false;
static bool g_switchScheduled = false;

static uint16_t g_initialChannel = 12;
static uint16_t g_newChannel = 25;
static uint16_t g_currentChannel = 12;

static double g_edThreshold = -60.0;
static double g_currentEd = -95.0;

static Time g_edCheckInterval = MilliSeconds(100);
static Time g_switchDelay = MilliSeconds(500);

static Time g_interferenceStart = Seconds(0);
static Time g_switchStart = Seconds(0);
static Time g_switchEnd = Seconds(0);

static uint64_t g_rxBytesThisSecond = 0;
static uint64_t g_totalRxBytes = 0;

static Ptr<Socket> g_sinkSocket;
static Ipv6Address g_coordinatorAddress;

void
DoChannelSwitch()
{
    g_currentChannel = g_newChannel;
    g_switched = true;
    g_switchScheduled = false;
    g_switchEnd = Simulator::Now();

    std::cout << "\n=== CHANNEL SWITCH COMPLETED ===" << std::endl;
    std::cout << "Time: " << g_switchEnd.GetSeconds() << " s" << std::endl;
    std::cout << "Old channel: " << g_initialChannel << std::endl;
    std::cout << "New channel: " << g_newChannel << std::endl;
    std::cout << "Switching latency: "
              << (g_switchEnd - g_switchStart).GetMilliSeconds()
              << " ms" << std::endl
              << std::endl;
}

void
CheckEdAndMaybeSwitch()
{
    if (g_wifiBusy && !g_switched)
    {
        g_currentEd = -35.0;
    }
    else
    {
        g_currentEd = -95.0;
    }

    if (!g_switched && !g_switchScheduled && g_currentEd > g_edThreshold)
    {
        g_switchScheduled = true;
        g_switchStart = Simulator::Now();

        std::cout << "\n=== INTERFERENCE DETECTED ===" << std::endl;
        std::cout << "Time: " << g_switchStart.GetSeconds() << " s" << std::endl;
        std::cout << "Measured ED: " << g_currentEd << " dBm" << std::endl;
        std::cout << "Threshold: " << g_edThreshold << " dBm" << std::endl;
        std::cout << "Coordinator decides to switch from channel "
                  << g_currentChannel << " to channel "
                  << g_newChannel << std::endl;

        Simulator::Schedule(g_switchDelay, &DoChannelSwitch);
    }

    if (Simulator::Now() < Seconds(20.0))
    {
        Simulator::Schedule(g_edCheckInterval, &CheckEdAndMaybeSwitch);
    }
}

void
StartWifiInterference()
{
    g_wifiBusy = true;
    g_interferenceStart = Simulator::Now();

    std::cout << "\n=== WIFI HIGH-BANDWIDTH TRANSFER STARTED ===" << std::endl;
    std::cout << "Time: " << g_interferenceStart.GetSeconds() << " s" << std::endl;
    std::cout << "Coordinator now sees high ED on channel "
              << g_currentChannel << std::endl
              << std::endl;
}

void
StopWifiInterference()
{
    g_wifiBusy = false;

    std::cout << "\n=== WIFI HIGH-BANDWIDTH TRANSFER STOPPED ===" << std::endl;
    std::cout << "Time: " << Simulator::Now().GetSeconds() << " s" << std::endl
              << std::endl;
}

void
LrWpanReceive(Ptr<Socket> socket)
{
    Address from;
    Ptr<Packet> packet;

    while ((packet = socket->RecvFrom(from)))
    {
        if (!(g_wifiBusy && !g_switched))
        {
            g_rxBytesThisSecond += packet->GetSize();
            g_totalRxBytes += packet->GetSize();
        }
    }
}

void
SendLrWpanPacket(Ptr<Socket> socket, uint32_t packetSize, Time interval)
{
    Ptr<Packet> packet = Create<Packet>(packetSize);
    socket->Send(packet);

    if (Simulator::Now() + interval < Seconds(20.0))
    {
        Simulator::Schedule(interval, &SendLrWpanPacket, socket, packetSize, interval);
    }
}

void
PrintThroughput()
{
    double kbps = (g_rxBytesThisSecond * 8.0) / 1000.0;

    std::cout << "Time " << Simulator::Now().GetSeconds()
              << " s | 802.15.4 throughput = "
              << kbps << " kbps" << std::endl;

    g_rxBytesThisSecond = 0;

    if (Simulator::Now() < Seconds(20.0))
    {
        Simulator::Schedule(Seconds(1.0), &PrintThroughput);
    }
}

int
main(int argc, char* argv[])
{
    Time::SetResolution(Time::NS);

    uint32_t nLrWpan = 11;    
    uint32_t nWifiTotal = 7;  
    uint32_t nWifiSta = 6;    // 1 AP + 6 stations
    uint32_t packetSize = 40;
    double simTime = 20.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("packetSize", "Packet size for 802.15.4 traffic", packetSize);
    cmd.Parse(argc, argv);

    std::cout << "Simulation begun." << std::endl;
    std::cout << "802.15.4 nodes: " << nLrWpan << std::endl;
    std::cout << "802.11 nodes: " << nWifiTotal << " (1 AP + 6 STAs)" << std::endl;
    std::cout << "Initial 802.15.4 channel: " << g_initialChannel << std::endl;
    std::cout << "Target 802.15.4 channel: " << g_newChannel << std::endl
              << std::endl;

    NodeContainer lrwpanNodes;
    lrwpanNodes.Create(nLrWpan);

    NodeContainer wifiApNode;
    wifiApNode.Create(1);

    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(nWifiSta);

    MobilityHelper mobility;

    Ptr<ListPositionAllocator> lrPos = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < nLrWpan; ++i)
    {
        lrPos->Add(Vector(5.0 + i * 2.0, 5.0, 0.0));
    }
    mobility.SetPositionAllocator(lrPos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(lrwpanNodes);

    Ptr<ListPositionAllocator> wifiPos = CreateObject<ListPositionAllocator>();
    wifiPos->Add(Vector(10.0, 15.0, 0.0)); // AP
    for (uint32_t i = 0; i < nWifiSta; ++i)
    {
        wifiPos->Add(Vector(5.0 + i * 2.0, 18.0, 0.0));
    }

    NodeContainer allWifiNodes;
    allWifiNodes.Add(wifiApNode);
    allWifiNodes.Add(wifiStaNodes);

    mobility.SetPositionAllocator(wifiPos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allWifiNodes);

    // 802.15.4 + 6LoWPAN network
    LrWpanHelper lrWpanHelper;
    g_lrwpanDevices = lrWpanHelper.Install(lrwpanNodes);
    lrWpanHelper.CreateAssociatedPan(g_lrwpanDevices, 0x1234);

    SixLowPanHelper sixlowpan;
    NetDeviceContainer sixlowpanDevices = sixlowpan.Install(g_lrwpanDevices);

    InternetStackHelper internetv6;
    internetv6.Install(lrwpanNodes);

    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:1::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer lrIfaces = ipv6.Assign(sixlowpanDevices);

    g_coordinatorAddress = lrIfaces.GetAddress(0, 1);

    TypeId udpFactory = TypeId::LookupByName("ns3::UdpSocketFactory");

    g_sinkSocket = Socket::CreateSocket(lrwpanNodes.Get(0), udpFactory);
    Inet6SocketAddress local = Inet6SocketAddress(Ipv6Address::GetAny(), 9999);
    g_sinkSocket->Bind(local);
    g_sinkSocket->SetRecvCallback(MakeCallback(&LrWpanReceive));

    for (uint32_t i = 1; i < nLrWpan; ++i)
    {
        Ptr<Socket> srcSocket = Socket::CreateSocket(lrwpanNodes.Get(i), udpFactory);
        Inet6SocketAddress remote = Inet6SocketAddress(g_coordinatorAddress, 9999);
        srcSocket->Connect(remote);

        Simulator::Schedule(Seconds(1.0 + i * 0.05),
                            &SendLrWpanPacket,
                            srcSocket,
                            packetSize,
                            MilliSeconds(100));
    }

    // 802.11 Wi-Fi network
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("ErpOfdmRate54Mbps"),
                                 "ControlMode",
                                 StringValue("ErpOfdmRate6Mbps"));

    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(wifiChannel.Create());

    WifiMacHelper wifiMac;
    Ssid ssid = Ssid("interference-network");

    wifiMac.SetType("ns3::StaWifiMac",
                    "Ssid",
                    SsidValue(ssid),
                    "ActiveProbing",
                    BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(wifiPhy, wifiMac, wifiStaNodes);

    wifiMac.SetType("ns3::ApWifiMac",
                    "Ssid",
                    SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(wifiPhy, wifiMac, wifiApNode);

    InternetStackHelper internetv4;
    internetv4.Install(allWifiNodes);

    NetDeviceContainer wifiDevices;
    wifiDevices.Add(apDevice);
    wifiDevices.Add(staDevices);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiIfaces = ipv4.Assign(wifiDevices);

    uint16_t sinkPort = 5000;
    Address sinkAddress(InetSocketAddress(wifiIfaces.GetAddress(0), sinkPort));

    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory",
                                      InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApp = packetSinkHelper.Install(wifiApNode.Get(0));
    sinkApp.Start(Seconds(5.5));
    sinkApp.Stop(Seconds(simTime));

    for (uint32_t i = 0; i < nWifiSta; ++i)
    {
        BulkSendHelper bulkSender("ns3::TcpSocketFactory", sinkAddress);
        bulkSender.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer app = bulkSender.Install(wifiStaNodes.Get(i));
        app.Start(Seconds(6.0));
        app.Stop(Seconds(18.0));
    }

    wifiPhy.EnablePcap("assignment-wifi-ap", apDevice.Get(0), true);
    wifiPhy.EnablePcap("assignment-wifi-sta0", staDevices.Get(0), true);

    // Schedule events
    Simulator::Schedule(Seconds(2.0), &PrintThroughput);
    Simulator::Schedule(Seconds(6.0), &StartWifiInterference);
    Simulator::Schedule(Seconds(6.0), &CheckEdAndMaybeSwitch);
    Simulator::Schedule(Seconds(18.0), &StopWifiInterference);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    std::cout << "\n=== FINAL RESULTS ===" << std::endl;
    std::cout << "Total received 802.15.4 data: " << g_totalRxBytes << " bytes" << std::endl;

    if (g_switched)
    {
        std::cout << "Interference started at: " << g_interferenceStart.GetSeconds() << " s" << std::endl;
        std::cout << "Switch started at: " << g_switchStart.GetSeconds() << " s" << std::endl;
        std::cout << "Switch completed at: " << g_switchEnd.GetSeconds() << " s" << std::endl;
        std::cout << "Switching latency: "
                  << (g_switchEnd - g_switchStart).GetMilliSeconds()
                  << " ms" << std::endl;
    }
    else
    {
        std::cout << "No channel switch happened." << std::endl;
    }

    Simulator::Destroy();
    return 0;
}