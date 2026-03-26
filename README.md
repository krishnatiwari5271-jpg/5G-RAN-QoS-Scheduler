# 5G RAN QoS-Aware MAC Scheduler

## 📌 Overview
This project simulates a 5G RAN MAC Scheduler focusing on QoS-aware resource allocation. It models how PRBs (Physical Resource Blocks) are allocated among users based on priority, GBR requirements, and channel conditions.

---

## ⚙️ Features
- PRB Allocation Engine
- QoS-aware Scheduling (GBR + Priority)
- SINR-based MCS Adaptation
- Throughput Calculation per user
- KPI Analysis:
  - PRB Utilization
  - Throughput
  - GBR Satisfaction
  - Congestion Behavior

---

## 📊 Key Concepts Implemented
- RAN (Radio Access Network)
- MAC Scheduler Logic
- GBR (Guaranteed Bit Rate)
- SINR → MCS Mapping
- BLER Awareness (link reliability)
- Resource Fairness vs Throughput Trade-off

 Simulation Scenarios
- Low load vs High congestion
- Multi-user scheduling (3 / 5 / 8 users)
- GBR vs Non-GBR traffic
- SINR variation impact

---

## 🛠️ Tech Stack
- C++
- Basic Networking Concepts (TCP/IP understanding)
- Linux Environment

---

## 🚀 How to Run
```bash
g++ main.cpp -o scheduler
./scheduler
