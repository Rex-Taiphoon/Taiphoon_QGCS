# ADR-0014：內嵌 CycloneDDS 的 ROS2 線上相容層

**日期**：2026-10-12
**狀態**：已接受（M1 PoC 驗證後轉為已實作）

## 背景

Taiphoon_Agent 以 **ROS2 Humble + `rmw_cyclonedds_cpp`** 執行。原 GCS 直接連結 rclcpp，
因此必須安裝 ROS2；ROS2 在 macOS 僅為 Tier 3 支援。Agent 端不可修改。

## 決策

- 程式**不連結任何 ROS2 函式庫**，改為內嵌 **CycloneDDS（C 與 C++ binding）**，
  在網路層產生與 ROS2 Humble + rmw_cyclonedds 相同的 DDS 實體。
- CycloneDDS 版本固定為 Humble 對應的 **0.10.x**，實際版本於 M1 確認後寫入 CMake。
- **部署前提**：Agent 必須使用 `rmw_cyclonedds_cpp`。一般 topic 跨 DDS 廠商可互通，
  但 service 的請求標頭由各 RMW 自訂，使用 Fast DDS 的 Agent 無法呼叫 shutdown service。
- 命名對應：

  | ROS2 概念 | DDS 實際名稱 |
  |---|---|
  | Topic `/uav_1_agent/state` | `rt/uav_1_agent/state` |
  | 型別 `taiphoon_agent/msg/AgentState` | `taiphoon_agent::msg::dds_::AgentState_` |
  | `std_msgs/msg/String` | `std_msgs::msg::dds_::String_` |
  | Service `/uav_1_agent/shutdown` | `rq/uav_1_agent/shutdownRequest`、`rr/uav_1_agent/shutdownReply` |

- 型別產生：`msg/` 同步權威介面 → `tools/msg2idl.py` 轉為 IDL → `idlc` 產生 C++ 型別，
  整合於 CMake configure 階段；`std_msgs/String`、`std_srvs/SetBool`、`builtin_interfaces/Time` 另以手寫 IDL 提供。
- QoS 依介面文件第 8 節集中於單一表格（程式碼），並以單元測試核對。
- 機隊發現改讀 DDS 內建 topic `DCPSPublication`，去除 `rt/` 前綴後沿用 `uav_{N}_agent/state` 規則。
- Service 呼叫依 rmw_cyclonedds 格式在樣本前附加請求標頭（client GUID + sequence number），以 sequence number 比對回應。
- CycloneDDS 設定由程式依站點設定產生 XML 字串，透過 `dds_create_domain()` 套用，
  延續 ADR-0011 的單播、`ParticipantIndex=auto`、`MaxAutoParticipantIndex=20` 等參數；SITL 可使用 multicast。
- 相容層位於 `src/link/`，對核心只提供 `IAgentLink` 介面（見 ADR-0015）。

## M1 必須驗證的項目

1. 訂閱 `/uav_N_agent/state` 等 topic，欄位解碼與 Agent 一致。
2. 發布 `/uav_N_agent/command` 後 Agent 正確執行。
3. `DCPSPublication` 能穩定反映 Agent 上線、離線與 shutdown 後 topic 消失。
4. Shutdown service 請求標頭格式與回應比對正確。
5. 本機產生的型別資訊（XTypes）不影響與 rmw_cyclonedds 端點配對。

任一項無法通過時，停止 UI 開發並重新評估本決策。

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| 隨程式打包 rclcpp | Windows 打包體積大且複雜；macOS 無官方支援 |
| 修改 Agent 改用其他協定（MQTT、gRPC 等） | 違反「Agent 端不修改」 |
| zenoh／DDS router 橋接 | 增加部署元件，與「直接安裝即可使用」衝突 |

## 後果

- 沒有 `ros2` CLI，原除錯終端機需改為內建唯讀工具。
- `msg/` 更新後需重新 configure 以產生型別；介面變更仍以 `../Taiphoon_Agent_Interfaces/` 為唯一權威來源。
- Windows 上的 Agent SITL 位於 Docker Desktop NAT 之後，互通測試需使用區網 Linux 主機／VM 或 host networking。
