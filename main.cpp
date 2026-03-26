#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <complex>
#include <cstdint> // Necessary for fixed-width types like uint8_t, uint32_t
#include <fstream>
// --- GLOBAL STRUCTURES ---

struct ChannelConfig {
    int priority;
    int pbr_kbps;
    int bsd_ms;
    long pending_bits;
    int sdu_size_bytes;
};

struct SystemConfig {
    // PHY/MAC Parameters
    int FR_type = 1;
    int nPRB =80;
    const int subcarriersPerRB = 12;
    const int symbolsPerSlot = 14;
    int nlayers = 2;
    int Nid = 100;
    double fs = 30.72e6;

    // Modulation and Coding
    std::string modulation = "QPSK";
    int qm = 2;

    // Derived Parameters
    int totalSubcarriers = nPRB * subcarriersPerRB;
    int NRE = totalSubcarriers * symbolsPerSlot;

    // MAC Layer Parameters
    int nUsers = 8;
    double TTI = 1e-3;
    int bitsPerRB;

    // RNTI (Radio Network Temporary Identifier)
    std::vector<int> RNTI = {1, 2, 3,4,5,6,7,8};

    SystemConfig(const std::string& mod = "QPSK") : modulation(mod) {
        if (mod == "QPSK") qm = 2;
        else if (mod == "16QAM") qm = 4;
        else if (mod == "64QAM") qm = 6;
        else {
            std::cerr << "Warning: Invalid modulation type. Using QPSK (qm=2) as default.\n";
            qm = 2;
            modulation = "QPSK";
        }

        // Dynamic Calculation of bitsPerRB based on PHY
        int REsPerRB = subcarriersPerRB * symbolsPerSlot;
        // 0.95 factor accounts for Reference Signals and control overhead
        bitsPerRB = static_cast<int>(std::floor(REsPerRB * qm * 0.95));

        std::cout << "System Setup: " << nPRB << " PRBs, Modulation: " << modulation
                  << " (qm=" << qm << "). Theoretical bitsPerRB: " << bitsPerRB << ".\n";
    }
};

struct PDCP_PDU_Info {
    std::vector<uint8_t> pduData;
    uint32_t sequenceNumber;
};

struct RLC_PDU_Info {
    std::vector<uint8_t> pduData;
    size_t sizeBits;
    uint32_t sequenceNumber;
};

struct TransportBlock {
    std::vector<uint8_t> tbBytes;
    int allocatedPRBs;
};


// --- APPLICATION LAYER (Setup and Data Generation) ---

class ApplicationLayer {
private:
    std::vector<ChannelConfig> config;

    std::vector<ChannelConfig> initialize_qos_channels() {
        std::vector<ChannelConfig> channelConfig;

// 1. Emergency Stop (URLLC)
channelConfig.push_back({1, 0, 5, 1200, 32});

// 2. Flight Control (Mission Critical)
channelConfig.push_back({3, 5000, 5, 5000, 128});

// 3. Video Monitoring (High Throughput)
channelConfig.push_back({8, 20000, 300, 50000, 1400});

// 4. Sensor Telemetry
channelConfig.push_back({5, 1000, 50, 3000, 256});

// 5. Command & Control
channelConfig.push_back({4, 2000, 20, 2000, 128});

// 6. Video Stream 2
channelConfig.push_back({8, 15000, 300, 40000, 1200});

// 7. Background Data
channelConfig.push_back({9, 0, 100, 20000, 512});

// 8. Best Effort Data
channelConfig.push_back({10, 0, 100, 15000, 512});


        return channelConfig;
    }

public:
    ApplicationLayer() {
        config = initialize_qos_channels();
    }

    const std::vector<ChannelConfig>& getChannelConfig() const {
        return config;
    }

    std::vector<uint8_t> generateMockData(size_t size) const {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i % 256);
        }
        return data;
    }
};


// --- PDCP LAYER (Security & Sequence Numbering) ---

class PDCP_Layer {
private:
    const SystemConfig& sysConfig;
    uint32_t nextSN;

    std::vector<uint8_t> mockSecurity(const std::vector<uint8_t>& data, int RNTI) {
        std::vector<uint8_t> securedData = data;
        uint8_t key = static_cast<uint8_t>(sysConfig.Nid ^ RNTI);
        for (uint8_t& byte : securedData) {
            byte = byte ^ key;
        }
        std::cout << "\t-> PDCP: Data encrypted/integrity checked.\n";
        return securedData;
    }

    std::vector<uint8_t> addPDCPHeader(const std::vector<uint8_t>& securedData, uint32_t sn) {
        std::vector<uint8_t> pdu = securedData;
        uint16_t sn_12bit = static_cast<uint16_t>(sn & 0x0FFF);
        uint8_t sn_byte1 = static_cast<uint8_t>((sn_12bit >> 8) & 0xFF);
        uint8_t sn_byte2 = static_cast<uint8_t>(sn_12bit & 0xFF);
        pdu.insert(pdu.begin(), {sn_byte1, sn_byte2});
        return pdu;
    }

public:
    PDCP_Layer(const SystemConfig& config) : sysConfig(config), nextSN(0) {}

    PDCP_PDU_Info process(const std::vector<uint8_t>& appData, int RNTI) {
        std::cout << "\n--- PDCP Layer Processing (RNTI: " << RNTI << ") ---\n";
        std::vector<uint8_t> securedData = mockSecurity(appData, RNTI);
        uint32_t currentSN = nextSN++;
        std::vector<uint8_t> finalPDU = addPDCPHeader(securedData, currentSN);

        std::cout << "\t-> PDCP: PDU size " << finalPDU.size() << " bytes. SN: " << currentSN << ".\n";
        return {finalPDU, currentSN};
    }
};


// --- RLC LAYER (Segmentation & Reordering) ---

class RLC_Layer {
private:
    const SystemConfig& sysConfig;
    uint32_t nextSN;

    std::vector<uint8_t> addRLCHeader(const std::vector<uint8_t>& segment, uint32_t sn) {
        std::vector<uint8_t> pdu = segment;
        uint16_t sn_12bit = static_cast<uint16_t>(sn & 0x0FFF);
        uint8_t sn_byte1 = static_cast<uint8_t>((sn_12bit >> 8) & 0xFF);
        uint8_t sn_byte2 = static_cast<uint8_t>(sn_12bit & 0xFF);
        pdu.insert(pdu.begin(), {sn_byte1, sn_byte2});
        return pdu;
    }

public:
    RLC_Layer(const SystemConfig& config) : sysConfig(config), nextSN(0) {}

    std::vector<RLC_PDU_Info> process(const PDCP_PDU_Info& pdcpPDU, int RNTI, int maxSegmentSizeBytes) {
        std::cout << "--- RLC Layer Processing (RNTI: " << RNTI << ") ---\n";
        std::vector<RLC_PDU_Info> rlcPDUs;
        const std::vector<uint8_t>& inputData = pdcpPDU.pduData;

        const int RLC_HEADER_SIZE = 2;
        int maxPayloadSize = maxSegmentSizeBytes - RLC_HEADER_SIZE;

        if (maxPayloadSize <= 0) {
             maxPayloadSize = maxSegmentSizeBytes;
        }

        size_t currentPosition = 0;

        // 1. Segmentation Loop
        while (currentPosition < inputData.size()) {
            size_t remainingBytes = inputData.size() - currentPosition;
            size_t segmentSize = std::min(remainingBytes, (size_t)maxPayloadSize);

            std::vector<uint8_t> segment(
                inputData.begin() + currentPosition,
                inputData.begin() + currentPosition + segmentSize
            );

            // 2. Add RLC Header
            uint32_t currentSN = nextSN++;
            std::vector<uint8_t> finalPDU = addRLCHeader(segment, currentSN);

            rlcPDUs.push_back({
                finalPDU,
                finalPDU.size() * 8, // Size in bits
                currentSN
            });

            currentPosition += segmentSize;
        }

        std::cout << "\t-> RLC: Segmented PDCP PDU into " << rlcPDUs.size()
                  << " RLC PDUs (Max Segment Size: " << maxSegmentSizeBytes << " bytes).\n";
        return rlcPDUs;
    }
};

class MAC_Layer {
private:
    const double TTI;
    const int nPRB;

public:
    MAC_Layer(const SystemConfig& config)
        : TTI(config.TTI),
          nPRB(config.nPRB)
    {}

    TransportBlock processMACTraffic(
        const std::vector<std::vector<RLC_PDU_Info>>& rlcPDUs,
        const ApplicationLayer& appLayer,
        const SystemConfig& config)
    {
        std::cout << "\n--- MAC Layer Processing (Hybrid + KPI) ---\n";
        std::ofstream file("results.csv", std::ios::app);

        const auto& channels = appLayer.getChannelConfig();

        // 🔥 ADD THIS SINR VECTOR
        std::vector<double> sinr =
{15,12,10,8,6,18,5,3};
        auto sinrToQM = [](double sinr_dB)
        {
            if (sinr_dB >= 13) return 6;
            if (sinr_dB >= 7)  return 4;
            if (sinr_dB >= 2)  return 2;
            return 1;
        };

        std::vector<int> rbAllocation(config.nUsers, 0);
        int remainingPRB = nPRB;

        // ==============================
        // PHASE 1: STRICT PRIORITY
        // ==============================
        for (size_t u = 0; u < config.nUsers; ++u)
        {
            if (channels[u].priority == 1 && remainingPRB > 0)
            {
                rbAllocation[u] = 1;
                remainingPRB--;
                std::cout << "Strict Priority → U"
                          << u+1 << " = 1 PRB\n";
            }
        }

        // ==============================
        // PHASE 2: GBR Allocation
        // ==============================
        for (size_t u = 0; u < config.nUsers; ++u)
        {
            if (channels[u].pbr_kbps > 0 && remainingPRB > 0)
            {
                double requiredBits =
                    channels[u].pbr_kbps * 1000 * TTI;

                int qm = sinrToQM(sinr[u]);

                int bitsPerRB_dynamic =
                    qm * config.subcarriersPerRB *
                    config.symbolsPerSlot;

                int requiredPRB =
                    std::ceil(requiredBits /
                              bitsPerRB_dynamic);

                int alloc =
                    std::min(requiredPRB,
                             remainingPRB);

                rbAllocation[u] += alloc;
                remainingPRB -= alloc;

                std::cout << "GBR Phase → U"
                          << u+1 << " = "
                          << rbAllocation[u]
                          << " PRB\n";
                          
            }
        }

        // ==============================
        // PRINT FINAL ALLOCATION
        // ==============================
        std::cout << "\nFinal PRB Allocation: ";
        for (size_t u = 0; u < config.nUsers; ++u)
            std::cout << "U" << u+1
                      << "=" << rbAllocation[u]
                      << " ";
        std::cout << "\n";

        int usedPRB = 0;
        for (int rb : rbAllocation)
            usedPRB += rb;

        std::cout << "KPI → PRB Utilization: "
                  << (usedPRB * 100.0 / nPRB)
                  << " %\n";

        // ==============================
        // NEW THROUGHPUT CALCULATION
        // ==============================
        for (size_t u = 0; u < config.nUsers; ++u)
        {
            int qm = sinrToQM(sinr[u]);

            int bitsPerRB_dynamic =
                qm * config.subcarriersPerRB *
                config.symbolsPerSlot;

            int bitsPerTTI =
                rbAllocation[u] * bitsPerRB_dynamic;

            double throughputMbps =
                bitsPerTTI * 1000.0 / 1e6;

            std::cout << "User "
                      << u+1
                      << " Throughput ≈ "
                      << throughputMbps
                      << " Mbps\n";

            if (channels[u].pbr_kbps > 0)
            {
                double gbrMbps =
                    channels[u].pbr_kbps / 1000.0;

                std::cout << "GBR Status: "
                          << (throughputMbps >= gbrMbps
                              ? "SUCCESS"
                              : "FAIL")
                          << "\n";
                          file << u+1 << ","
                               << rbAllocation[u] << ","
                               << throughputMbps << "\n";
            }
        }
        file.close();

        std::vector<uint8_t> finalTB;

        return {finalTB, usedPRB};
    }
};
// --- PHY LAYER (Coding, Modulation, OFDM) ---

class PHY_Layer {
private:
    const SystemConfig& sysConfig;

    double mockChannelCondition(int RNTI) {
        if (RNTI == 1) return 15.0; // High SINR (ULL)
        if (RNTI == 2) return 10.0; // Medium SINR
        if (RNTI == 3) return 20.0;  // high SINR (Video)
        return 8.0;
    }

    int calculateSINR_to_qm(double sinr_dB, int RNTI) {
        std::cout << "   CQI Feedback: SINR=" << sinr_dB << " dB.\n";

        if (sinr_dB >= 13.0) return 6; // 64QAM
        if (sinr_dB >= 7.0)  return 4; // 16QAM
        if (sinr_dB >= 2.0)  return 2; // QPSK

        if (RNTI == 1) return 2; // ULL fallback to robust QPSK

        return 0; // No transmission possible
    }

    std::vector<int> mockLDPCEncode(const std::vector<int>& tbCrcBits) {
        size_t inputSize = tbCrcBits.size();
        size_t encodedSize = static_cast<size_t>(std::ceil(inputSize * 1.05));
        std::vector<int> encodedBits(encodedSize, 0);
        std::copy(tbCrcBits.begin(), tbCrcBits.end(), encodedBits.begin());
        return encodedBits;
    }

    std::vector<int> mockRateMatchLDPC(const std::vector<int>& encodedCB, int E_user_bits) {
        std::vector<int> rateMatchedBits;
        if (E_user_bits <= 0) return {};

        if (E_user_bits >= encodedCB.size()) {
            rateMatchedBits = encodedCB;
        } else {
            rateMatchedBits.resize(E_user_bits);
            std::copy(encodedCB.begin(), encodedCB.begin() + E_user_bits, rateMatchedBits.begin());
        }
        return rateMatchedBits;
    }

    std::vector<int> mockScramble(const std::vector<int>& dataBits, int Nid, int RNTI) {
        std::vector<int> scrambledBits = dataBits;
        if (!scrambledBits.empty()) {
            for (size_t i = 0; i < scrambledBits.size(); ++i) {
                int mockSequence = (RNTI + i) % 2;
                scrambledBits[i] = scrambledBits[i] ^ mockSequence;
            }
            std::cout << "\t-> Data Scrambled using Nid=" << Nid << " and RNTI=" << RNTI << ".\n";
        }
        return scrambledBits;
    }

    std::vector<std::complex<double>> mockModulationMap(const std::vector<int>& dataBits, int qm) {
        int bitsPerSymbol = qm;
        std::vector<std::complex<double>> complexSymbols;
        for (size_t i = 0; i < dataBits.size(); i += bitsPerSymbol) {
            if (qm >= 2) {
                // Simplified QPSK mapping
                double real = (dataBits[i] == 0) ? 1.0 : -1.0;
                double imag = (i + 1 < dataBits.size() && dataBits[i+1] == 0) ? 1.0 : -1.0;
                complexSymbols.push_back({real / std::sqrt(2.0), imag / std::sqrt(2.0)});
            }
        }
        std::cout << "\t-> Data Modulated to " << complexSymbols.size() << " complex symbols (using qm=" << qm << ").\n";
        return complexSymbols;
    }

    std::vector<std::complex<double>> mockReferenceSignalInsertion(
        const std::vector<std::complex<double>>& dataSymbols,
        int allocatedREs)
    {
        size_t totalREs = allocatedREs;
        std::vector<std::complex<double>> grid(totalREs);
        size_t dataIdx = 0;

        for (size_t i = 0; i < totalREs; ++i) {
            if (i % 10 == 0) {
                grid[i] = {1.0, 1.0}; // Mock DMRS symbol
            } else if (dataIdx < dataSymbols.size()) {
                grid[i] = dataSymbols[dataIdx++];
            } else {
                grid[i] = {0.0, 0.0};
            }
        }
        std::cout << "\t-> DMRS/CSI Signals inserted into the resource grid (10% overhead).\n";
        return grid;
    }

    std::vector<std::complex<double>> mockOFDM(
        const std::vector<std::complex<double>>& resourceGrid)
    {
        std::cout << "\t-> IFFT/OFDM process complete. Cyclic Prefix added. Signal ready for DAC/RF.\n";
        return resourceGrid;
    }


public:
    PHY_Layer(const SystemConfig& config) : sysConfig(config) {}

    std::vector<std::complex<double>> processTransmission(
        const TransportBlock& tb,
        int RNTI,
        int PRB_Allocation)
    {
        std::cout << "\n--- PHY Layer Processing (RNTI=" << RNTI << ", PRBs=" << PRB_Allocation << ") ---\n";

        // Step 1: Dynamic CQI/qm Selection
        double sinr = mockChannelCondition(RNTI);
        int dynamic_qm = calculateSINR_to_qm(sinr, RNTI);

        if (dynamic_qm == 0) {
            std::cout << "!!! CQI TOO LOW. TRANSMISSION BLOCKED. !!!\n";
            return {};
        }

        // Step 2: Resource Calculation for Rate Matching
        int allocatedREs = PRB_Allocation * (sysConfig.subcarriersPerRB * sysConfig.symbolsPerSlot);
        int E_user_bits = allocatedREs * dynamic_qm;

        std::cout << "   RRM Final QM: " << dynamic_qm << " (Modulation: " << (dynamic_qm == 2 ? "QPSK" : dynamic_qm == 4 ? "16QAM" : "64QAM") << ").\n";
        std::cout << "   E_user (Capacity of " << PRB_Allocation << " PRBs): " << E_user_bits << " bits.\n";

        // Step 3: Coding Chain
        std::vector<int> tbBits;
        for (uint8_t byte : tb.tbBytes) {
            for (int i = 7; i >= 0; --i) {
                tbBits.push_back((byte >> i) & 1);
            }
        }
        std::cout << "1. Transport Block (TB) Payload Bits: " << tbBits.size() << " bits (Approx).\n";

        std::vector<int> tbCrcBits = tbBits;
        tbCrcBits.resize(tbCrcBits.size() + 24, 0); // CRC Attachment

        std::vector<int> encodedCB = mockLDPCEncode(tbCrcBits);
        std::vector<int> rateMatchedBits = mockRateMatchLDPC(encodedCB, E_user_bits);

        // Step 4: Scrambling and Modulation
        std::vector<int> scrambledBits = mockScramble(rateMatchedBits, sysConfig.Nid, RNTI);
        std::vector<std::complex<double>> dataSymbols = mockModulationMap(scrambledBits, dynamic_qm);

        // Step 5: Resource Grid Mapping and Reference Signals
        std::vector<std::complex<double>> resourceGrid = mockReferenceSignalInsertion(dataSymbols, allocatedREs);

        // Step 6: OFDM Modulation
        return mockOFDM(resourceGrid);
    }
};


// --- RX LAYER (Simple placeholder, not fully integrated into simulation loop below) ---

class RX_Layer {
public:
    RX_Layer(const SystemConfig& config) {}
    // Simplified placeholder as the main simulation only runs TX side
    void processReception(const std::vector<std::complex<double>>& timeDomainSignal, int RNTI) {
        // This is a placeholder for the actual reception logic, which isn't executed in the main loop below.
    }
};


// --- MAIN SIMULATION LOOP ---

int main() {
    // 1. SYSTEM INITIALIZATION
    SystemConfig sysConfig("QPSK");
    ApplicationLayer appLayer;

    // 2. LAYER INSTANTIATION
    PDCP_Layer pdcpLayer(sysConfig);
    RLC_Layer rlcLayer(sysConfig);
    MAC_Layer macLayer(sysConfig);
    PHY_Layer phyLayer(sysConfig);
    // RX_Layer rxLayer(sysConfig); // Not used in TX simulation loop

    const auto& qosConfig = appLayer.getChannelConfig();

    std::cout << "\n======================================================\n";
    std::cout << "  IIOT QoS END-TO-END STACK SIMULATION STARTED\n";
    std::cout << "======================================================\n";

    // 3. SIMULATE TRAFFIC FOR EACH USER (TTI by TTI)
    for (size_t u = 0; u < sysConfig.nUsers; ++u) {
        int RNTI = sysConfig.RNTI[u];
        const auto& channelConfig = qosConfig[u];

        std::cout << "\n\n--- PROCESSING USER " << RNTI << " (P="
                  << channelConfig.priority << ", "
                  << channelConfig.sdu_size_bytes << " B SDU) ---\n";

        // A. APPLICATION -> PDCP
        size_t appDataSize = 1024 + (u * 1024);
        std::vector<uint8_t> appData = appLayer.generateMockData(appDataSize);
        std::cout << "App Layer: Generating " << appDataSize << " bytes of data.\n";

        PDCP_PDU_Info pdcpPDU = pdcpLayer.process(appData, RNTI);

        // B. PDCP -> RLC (Segmentation)
        std::vector<RLC_PDU_Info> rlcPDUs = rlcLayer.process(
            pdcpPDU,
            RNTI,
            channelConfig.sdu_size_bytes
        );

        // C. RLC -> MAC (QoS Allocation and Multiplexing)
        // Mock MAC input: we only populate the current user's RLC PDU queue.
        std::vector<std::vector<RLC_PDU_Info>> rlcPDUs_mock_input(sysConfig.nUsers);
        rlcPDUs_mock_input[u] = rlcPDUs;

        TransportBlock tb = macLayer.processMACTraffic(
            rlcPDUs_mock_input,
            appLayer,
            sysConfig
        );

        // D. MAC -> PHY (CQI, Coding, Modulation, OFDM)
        if (tb.tbBytes.empty()) {
            std::cout << "PHY Layer: No Transport Block received from MAC.\n";
            continue;
        }

        std::vector<std::complex<double>> timeDomainSignal = phyLayer.processTransmission(
            tb,
            RNTI,
            tb.allocatedPRBs // Total PRBs are allocated to this TB in this simplified setup
        );

        // FINAL RESULT
        if (!timeDomainSignal.empty()) {
            std::cout << "\n[SUCCESS] User " << RNTI << " Signal transmitted with "
                      << timeDomainSignal.size() << " time-domain symbols.\n";
        }
    }

    std::cout << "\n======================================================\n";
    std::cout << "SIMULATION ENDED. CHECK LOGS FOR CQI/ALLOCATION DETAILS.\n";
    std::cout << "======================================================\n";
    return 0;
}