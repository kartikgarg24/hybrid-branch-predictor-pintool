#include "pin.H"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <iomanip>
#include <vector>
#include <algorithm>

KNOB<std::string> KnobTraceFile(KNOB_MODE_WRITEONCE, "pintool",
    "trace", "trace.txt", "Output file for instruction trace");

KNOB<std::string> KnobStatsFile(KNOB_MODE_WRITEONCE, "pintool",
    "stats", "branch_stats.txt", "Output file for branch statistics");

KNOB<UINT64> KnobMaxTrace(KNOB_MODE_WRITEONCE, "pintool",
    "max_trace", "100000", "Maximum instructions to trace (0 = unlimited)");

KNOB<UINT32> KnobGlobalBits(KNOB_MODE_WRITEONCE, "pintool",
    "global_bits", "12", "Index bits for global BHT (bimodal table, 2^N entries)");

KNOB<UINT32> KnobLocalHistBits(KNOB_MODE_WRITEONCE, "pintool",
    "local_hist_bits", "10", "Bits in per-branch local history register (LHR)");

KNOB<UINT32> KnobLocalBits(KNOB_MODE_WRITEONCE, "pintool",
    "local_bits", "10", "Index bits for local PHT (2^N entries)");

KNOB<UINT32> KnobMetaBits(KNOB_MODE_WRITEONCE, "pintool",
    "meta_bits", "12", "Index bits for meta-predictor table (2^N entries)");

enum SatCounter : uint8_t {
    STRONGLY_NOT_TAKEN = 0,
    WEAKLY_NOT_TAKEN   = 1,
    WEAKLY_TAKEN       = 2,
    STRONGLY_TAKEN     = 3
};

inline bool predictTaken(SatCounter s) { return s >= WEAKLY_TAKEN; }

inline SatCounter updateCounter(SatCounter s, bool taken) {
    if (taken)
        return (s == STRONGLY_TAKEN)     ? STRONGLY_TAKEN     : (SatCounter)(s + 1);
    else
        return (s == STRONGLY_NOT_TAKEN) ? STRONGLY_NOT_TAKEN : (SatCounter)(s - 1);
}


// --- Global (bimodal) predictor ---
// Indexed by PC & globalMask
static std::vector<SatCounter> globalBHT;
static UINT32 globalSize = 0;
static UINT32 globalMask = 0;

// --- Local predictor ---
static std::unordered_map<ADDRINT, UINT32> localLHR;   // per-PC history shift reg
static std::vector<SatCounter>             localPHT;   // indexed by LHR value
static UINT32 localHistMask = 0;   // masks LHR to localHistBits wide
static UINT32 localPHTMask  = 0;   // masks PHT index

// --- Meta-predictor ---
static std::vector<SatCounter> metaTable;
static UINT32 metaMask = 0;

struct BranchRecord {
    UINT64 total         = 0;
    UINT64 taken         = 0;
    UINT64 predGlobal    = 0;   // correct by global predictor
    UINT64 predLocal     = 0;   // correct by local predictor
    UINT64 predHybrid    = 0;   // correct by hybrid (final) predictor
    std::string disasm;
    UINT64 pattern       = 0;
    UINT32 patternLen    = 0;
};

static std::ofstream traceOut;
static std::ofstream statsOut;

static UINT64 totalInstructions  = 0;
static UINT64 totalBranches      = 0;
static UINT64 traceLimit         = 0;
static bool   traceLimitReached  = false;

static std::unordered_map<ADDRINT, BranchRecord> branchMap;

VOID RecordInstruction(ADDRINT pc, BOOL isBranch, const char* disasm) {
    if (traceLimitReached) return;

    totalInstructions++;
    if (traceLimit > 0 && totalInstructions > traceLimit) {
        traceLimitReached = true;
        traceOut << "... (trace limit reached) ...\n";
        return;
    }

    traceOut << std::hex << std::setw(16) << std::setfill('0') << pc
             << "  " << (isBranch ? "BRANCH    " : "NON-BRANCH")
             << "  " << disasm << "\n";
}

VOID RecordBranch(ADDRINT pc, BOOL taken) {
    totalBranches++;
    bool takenBool = (bool)taken;

    UINT32 gIdx        = (UINT32)(pc & globalMask);
    bool   globalPred  = predictTaken(globalBHT[gIdx]);

    UINT32& lhr       = localLHR[pc];          // auto-initialised to 0
    UINT32  phtIdx    = lhr & localPHTMask;
    bool    localPred = predictTaken(localPHT[phtIdx]);

    UINT32     mIdx      = (UINT32)(pc & metaMask);
    SatCounter metaCnt   = metaTable[mIdx];
    bool       useLocal  = predictTaken(metaCnt);   // true -> local, false -> global
    bool       hybridPred = useLocal ? localPred : globalPred;

    bool globalCorrect = (globalPred == takenBool);
    bool localCorrect  = (localPred  == takenBool);
    bool hybridCorrect = (hybridPred == takenBool);

    if (localCorrect && !globalCorrect)
        metaTable[mIdx] = updateCounter(metaCnt, true);   // bias toward local
    else if (globalCorrect && !localCorrect)
        metaTable[mIdx] = updateCounter(metaCnt, false);  // bias toward global

    globalBHT[gIdx] = updateCounter(globalBHT[gIdx], takenBool);

    localPHT[phtIdx] = updateCounter(localPHT[phtIdx], takenBool);
    // Shift new outcome into the LHR
    lhr = ((lhr << 1) | (takenBool ? 1u : 0u)) & localHistMask;

    BranchRecord& rec = branchMap[pc];
    rec.total++;
    if (takenBool)     rec.taken++;
    if (globalCorrect) rec.predGlobal++;
    if (localCorrect)  rec.predLocal++;
    if (hybridCorrect) rec.predHybrid++;

    if (rec.patternLen < 64) rec.patternLen++;
    rec.pattern = (rec.pattern << 1) | (takenBool ? 1u : 0u);
}

VOID Instruction(INS ins, VOID* v) {
    BOOL isBranch = INS_IsBranch(ins) || INS_IsCall(ins) || INS_IsRet(ins);

    static std::unordered_map<ADDRINT, std::string> disasmCache;
    ADDRINT pc = INS_Address(ins);
    if (disasmCache.find(pc) == disasmCache.end())
        disasmCache[pc] = INS_Disassemble(ins);
    const char* disasmStr = disasmCache[pc].c_str();

    INS_InsertCall(ins, IPOINT_BEFORE,
        (AFUNPTR)RecordInstruction,
        IARG_INST_PTR,
        IARG_BOOL, isBranch,
        IARG_PTR, disasmStr,
        IARG_END);

    if (INS_IsBranchOrCall(ins) && INS_HasFallThrough(ins)) {
        branchMap[pc].disasm = INS_Disassemble(ins);

        INS_InsertCall(ins, IPOINT_BEFORE,
            (AFUNPTR)RecordBranch,
            IARG_INST_PTR,
            IARG_BRANCH_TAKEN,
            IARG_END);
    }
}

VOID Fini(INT32 code, VOID* v) {
    UINT64 totalGlobal = 0, totalLocal = 0, totalHybrid = 0;
    for (auto& kv : branchMap) {
        totalGlobal += kv.second.predGlobal;
        totalLocal  += kv.second.predLocal;
        totalHybrid += kv.second.predHybrid;
    }

    auto acc = [&](UINT64 correct) -> double {
        return totalBranches > 0 ? 100.0 * correct / totalBranches : 0.0;
    };

    statsOut << std::fixed << std::setprecision(2);
    statsOut << "  CS204 Branch Analysis Report — Hybrid (Tournament) Predictor\n\n";
    statsOut << "Total instructions executed  : " << std::dec << totalInstructions << "\n";
    statsOut << "Total conditional branches   : " << totalBranches << "\n\n";

    // Hardware summary
    UINT32 globalTableSz = globalSize;
    UINT32 localHistBits = 0;
    { UINT32 m = localHistMask; while (m) { localHistBits++; m >>= 1; } }
    UINT32 localPHTSize  = localPHTMask + 1;
    UINT32 metaSize      = metaMask + 1;

    statsOut << "Hardware configuration:\n";
    statsOut << "  Global BHT          : " << globalTableSz << " entries × 2-bit\n";
    statsOut << "  Local history reg   : " << localHistBits << " bits per branch\n";
    statsOut << "  Local PHT           : " << localPHTSize  << " entries × 2-bit\n";
    statsOut << "  Meta-predictor      : " << metaSize      << " entries × 2-bit\n\n";

    statsOut << "Accuracy summary:\n";
    statsOut << "  Global predictor    : " << acc(totalGlobal)  << "%\n";
    statsOut << "  Local  predictor    : " << acc(totalLocal)   << "%\n";
    statsOut << "  Hybrid (tournament) : " << acc(totalHybrid)  << "%\n\n";

    // Per-branch table
    statsOut << std::string(110, '-') << "\n";
    statsOut << std::left
             << std::setw(18) << "PC"
             << std::setw(8)  << "Total"
             << std::setw(8)  << "Taken"
             << std::setw(10) << "Taken%"
             << std::setw(10) << "Global%"
             << std::setw(10) << "Local%"
             << std::setw(10) << "Hybrid%"
             << std::setw(20) << "Pattern"
             << "Disassembly\n";
    statsOut << std::string(110, '-') << "\n";

    std::vector<std::pair<ADDRINT, BranchRecord*>> sorted;
    for (auto& kv : branchMap)
        sorted.push_back({kv.first, &kv.second});
    std::sort(sorted.begin(), sorted.end(),
        [](auto& a, auto& b){ return a.second->total > b.second->total; });

    for (auto& [pc, rec] : sorted) {
        if (rec->total == 0) continue;
        double takenPct  = 100.0 * rec->taken       / rec->total;
        double globalPct = 100.0 * rec->predGlobal  / rec->total;
        double localPct  = 100.0 * rec->predLocal   / rec->total;
        double hybridPct = 100.0 * rec->predHybrid  / rec->total;

        std::string pat;
        UINT32 show = std::min(rec->patternLen, (UINT32)16);
        for (INT32 i = show - 1; i >= 0; i--)
            pat += ((rec->pattern >> i) & 1) ? 'T' : 'N';
        if (rec->patternLen > 16) pat = "..." + pat;

        statsOut << "0x" << std::hex << std::setw(16) << std::setfill('0') << pc
                 << std::dec << std::setfill(' ')
                 << std::setw(8)  << rec->total
                 << std::setw(8)  << rec->taken
                 << std::setw(9)  << takenPct  << "%"
                 << std::setw(9)  << globalPct << "%"
                 << std::setw(9)  << localPct  << "%"
                 << std::setw(9)  << hybridPct << "%"
                 << "  " << std::setw(18) << pat
                 << "  " << rec->disasm << "\n";
    }

    statsOut << "\nTrace written to: " << KnobTraceFile.Value() << "\n";

    traceOut.close();
    statsOut.close();

    std::cerr << "[branch_analyzer] Done."
              << "  Instructions=" << totalInstructions
              << "  Branches="     << totalBranches
              << "  GlobalAcc="    << acc(totalGlobal)  << "%"
              << "  LocalAcc="     << acc(totalLocal)   << "%"
              << "  HybridAcc="    << acc(totalHybrid)  << "%\n";
}

int main(int argc, char* argv[]) {
    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) {
        std::cerr << "Usage: pin -t branch_analyzer_hybrid.so [options] -- <app>\n"
                  << KNOB_BASE::StringKnobSummary() << "\n";
        return 1;
    }

    traceOut.open(KnobTraceFile.Value());
    statsOut.open(KnobStatsFile.Value());
    if (!traceOut || !statsOut) {
        std::cerr << "ERROR: Cannot open output files\n";
        return 1;
    }

    // --- Global BHT ---
    UINT32 gBits = std::min(KnobGlobalBits.Value(), (UINT32)20);
    globalSize   = 1u << gBits;
    globalMask   = globalSize - 1;
    globalBHT.assign(globalSize, WEAKLY_TAKEN);

    // --- Local history + PHT ---
    UINT32 lhBits    = std::min(KnobLocalHistBits.Value(), (UINT32)16);
    localHistMask    = (1u << lhBits) - 1;
    UINT32 lBits     = std::min(KnobLocalBits.Value(), (UINT32)20);
    UINT32 localSize = 1u << lBits;
    localPHTMask     = localSize - 1;
    localPHT.assign(localSize, WEAKLY_TAKEN);

    // --- Meta-predictor ---
    UINT32 mBits = std::min(KnobMetaBits.Value(), (UINT32)20);
    metaMask     = (1u << mBits) - 1;
    metaTable.assign(metaMask + 1, WEAKLY_TAKEN); // start neutral

    traceLimit = KnobMaxTrace.Value();

    traceOut << std::left
             << std::setw(18) << "PC"
             << std::setw(12) << "Type"
             << "Disassembly\n"
             << std::string(60, '-') << "\n";

    INS_AddInstrumentFunction(Instruction, nullptr);
    PIN_AddFiniFunction(Fini, nullptr);

    PIN_StartProgram();
    return 0;
}
