Branch Prediction Simulator (Intel Pin Tool)

This project is a hardware simulation tool built using the *Intel Pin* framework. It hooks into any running Linux program, monitors its assembly instructions in real-time, and simulates a **Hybrid (Tournament) Branch Predictor**—the same type of architecture used in classic high-performance processors like the Alpha 21264.

What Does This Tool Do?

When programs execute, they encounter decision points (`if-else` statements, loops, etc.) known as *branches*. Modern CPUs try to guess whether a branch will be *Taken (T)* or *Not Taken (N)* before it actually happens to speed up execution. 

This simulator models three strategies simultaneously to see which one guesses best:
1. *Global Predictor:* Makes a guess based purely on the memory address (PC) of the branch instruction.
2. *Local Predictor:* Keeps track of the past history of *that specific branch* (e.g., "this loop ran 4 times before") to make a smart guess.
3. *Meta-Predictor (The Referee):* Keeps score between the Global and Local predictors. If the Local predictor is doing a better job for a certain branch, the Meta-predictor chooses the Local guess.

Hardware Specifications

1. *Saturating Counters:* The simulator uses 2-bit counters to track history. Instead of changing its mind immediately on a single anomaly, it takes multiple consecutive wrong guesses to completely flip its prediction strategy (Strongly Taken ↔ Weakly Taken ↔ Weakly Not Taken ↔ Strongly Not Taken).
2. *Smart Initialization:* All tracking tables start in a neutral `WEAKLY_TAKEN` state, allowing the system to adapt rapidly to loops from the very first instruction.

How to Build & Run

# Prerequisites
1. *Intel Pin Kit (v3.x+):* Installed on your Linux machine.
2. A standard C++17 compiler (`g++`).

# 1. Compile the Tool
Set your Pin root path variable and compile the shared library using the project Makefile:
```bash
export PIN_ROOT=/path/to/your/pin-kit
make OBJDIR=obj/
