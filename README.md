CS204 Project - Branch Analyser and Predictor
Progress Write-Up
Date: April 13, 2026
Student: Kartik

Objective:

The project uses Intel PIN to instrument a running program and
study its branch behaviour. Three things are done:
  1. Generate an instruction trace
  2. Observe how branches behave (taken or not)
  3. Predict branch outcomes using a 2-bit saturating counter


What has been done so far:

PIN Tool Written
  A PIN tool (branch_analyzer.cpp) was written that plugs into
  any Linux binary. PIN intercepts every instruction at runtime
  without needing source code of the target program.

Instruction Trace
  Every instruction is logged to trace.txt with its address,
  whether it is a branch or not, and its disassembly. A limit
  knob (-max_trace) keeps the file size manageable.

Branch Pattern Tracking
  For every conditional branch the tool tracks how many times
  it was taken vs not-taken, and stores the last 16 outcomes
  as a T/N string (e.g. TTTNTTTN). This makes it easy to spot
  patterns like loop exits (TTTTN...) or always-taken branches.

Branch Predictor
  A bimodal predictor was implemented using a Branch History
  Table (BHT). Each entry is a 2-bit saturating counter:

    0 = Strongly Not-Taken
    1 = Weakly Not-Taken
    2 = Weakly Taken
    3 = Strongly Taken

  The table is indexed by PC % table_size (default 4096 entries).
  After each branch the counter is incremented or decremented
  based on whether the branch was actually taken. Prediction
  accuracy is reported per-branch and overall.

Output
  Two files are written when the program finishes:
    trace.txt        - full instruction trace
    branch_stats.txt - per-branch stats, patterns, accuracy

Status:

Done:
  - PIN tool written and compiling
  - Instruction trace generation
  - Branch classification and pattern logging
  - Bimodal predictor with accuracy reporting
  - All build errors fixed

Still to do:
  - Run on actual benchmarks and collect results
  - Analyse patterns and discuss findings in report