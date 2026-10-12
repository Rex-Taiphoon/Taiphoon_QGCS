# Taiphoon_QGCS 程式碼規範 (CODE_CONSTRAINT.md)

> 本文件由根目錄 `AGENTS.md` 引用，是 Taiphoon_QGCS 的程式碼風格與 QML 規範。
> 撰寫或修改任何程式碼前必須閱讀本文件，並依照等級規定執行。

## 身份設定

你是 Taiphoon_QGCS 的軟體開發專家，負責開發與 ROS2 線上協定相容的 Qt 原生地面控制站：
- C++17 核心模組（QtCore、signal／slot）
- CycloneDDS ROS2 相容層（不連結 ROS2 函式庫）
- Qt 6.11 + QML 使用者介面

生成的程式碼必須嚴格遵守以下全部規範。`AGENTS.md` 定義專案邊界與架構規則；本文件定義程式碼風格。衝突時，安全與架構規則以 `AGENTS.md` 為準，格式與命名以本文件為準。

---

## L1 安全級規範（必須遵守，違反 = 可能出事故）

### L1-01：禁止標頭檔中的 using namespace
- `.h` / `.hpp` 中禁止 `using namespace xxx;`
- `.cpp` 中可以使用，但建議避免

### L1-02：指標引數入口必須檢查 nullptr
- 函式引數為指標時，入口必須檢查 `if (ptr == nullptr)` 後再使用
- 檢查失敗時記錄日誌（`qCWarning` 等）並 `return` / 傳回錯誤

### L1-03：DDS 回呼必須檢查樣本有效性
- 讀取 DDS 樣本時必須確認 `SampleInfo` 的 `valid_data` 為真，dispose／unregister 樣本不得當成資料處理
- 解碼失敗或指標為空時以 `qCWarning` 記錄並 `return`
- DDS 回呼只做解碼與發出 signal，不得直接操作其他執行緒的 `QObject`

### L1-04：禁止空 catch 區塊
- `catch` 中必須有實質錯誤處理（至少記錄日誌）
- 禁止 `catch (...) { }`

### L1-05：必須檢查重要函式的回傳值
- 檔案讀寫、DDS 建立實體、`QFile::open` 等回傳值必須檢查
- 失敗時記錄日誌並優雅處理（`return` / error / 預設值）

### L1-06：浮點數比較使用 epsilon
- 禁止用 `==` / `!=` 直接比較浮點數
- 使用 `std::abs(a - b) < 1e-6`

### L1-07：陣列存取前檢查邊界
- 優先使用 `.at()`；或先檢查 `index >= 0 && index < size`

### L1-08：控制指令含 NaN / Inf 時必須拒絕
- 傳送速度、位置、角度等控制指令前，必須 `std::isnan()` / `std::isinf()` 檢查
- 含非法值時拒絕傳送並記錄日誌

### L1-09：標頭檔必須有防重複包含保護
- 所有 `.h` / `.hpp` 開頭加 `#pragma once`

### L1-10：所有檔案編碼統一 UTF-8 無 BOM
- 原始碼與 QML 使用 UTF-8 無 BOM；中文註解使用 UTF-8 儲存

### L1-11：使用 RAII 管理資源
- 檔案、DDS 實體、互斥鎖等使用 RAII（`std::lock_guard`、`std::unique_ptr`、CycloneDDS C++ 實體物件等）
- 禁止手動 `open` / `close`、`lock` / `unlock`

### L1-12：DDS reader / writer 必須作為類別成員持有
- DDS reader、writer 與 listener 以類別成員持有，生命週期與所屬物件一致
- 禁止在函式內區域性建立 reader（函式結束後訂閱靜默消失）

### L1-13：跨執行緒只能透過 queued connection
- DDS 執行緒、核心執行緒與 UI 主執行緒之間的資料傳遞一律使用 signal／slot 的 queued connection
- 跨執行緒傳遞的型別必須以 `Q_DECLARE_METATYPE` 註冊
- 禁止在非擁有者執行緒呼叫 `QObject` 方法或存取 QML 物件

---

## L2 規範級規範（必須遵守，違反 = 程式碼品質低劣）

### 註解規範（L2-01 ~ L2-05）

- L2-01：所有註解與文件使用繁體中文，禁止中國用語（例如「屏幕」「硬盤」「軟件」「默認」「激活」「線程」）
- L2-02：所有原始碼檔案必須有檔案頭註解（`@file`、`@brief`、`@author`、`@date`）
- L2-03：公開函式必須有 Doxygen 註解（`@brief`、`@param`、`@return`）
- L2-04：註解說明「為什麼」而非「是什麼」
- L2-05：禁止過時註解（程式碼變更時同步更新）

### 格式規範（L2-06 ~ L2-11）

- L2-06：Allman 括號風格（`{` 獨佔一行）
- L2-07：單行 `if` / `for` / `while` 必須以 `{}` 包裹
- L2-08：二元運算子兩側各一個空格
- L2-09：單行不超過 120 字元
- L2-10：禁止使用 `goto`
- L2-11：C++ 與 QML 使用 4 空格縮排

### 命名規範（L2-12 ~ L2-20）

- L2-12：禁止拼音命名，使用英文
- L2-13：常數 / 巨集使用 UPPER_SNAKE_CASE（`MAX_SPEED`）
- L2-14：布林變數以 `is_` / `has_` / `should_` / `can_` 為字首
- L2-15：全域變數以 `g_` 為字首
- L2-16：類別成員變數以底線結尾（`target_pos_`）
- L2-17：介面類別（只提供介面的抽象基底）以 `I` 為字首（`IAgentLink`）
- L2-18：DDS 回呼函式以 `_cb` 結尾（`state_cb`）
- L2-19：禁止魔法數字，使用有名常數並註明含義與單位
- L2-20：C++ 類別 / 結構使用 PascalCase，函式 / 方法 / signal / slot 使用 snake_case，變數使用 snake_case，命名空間使用 snake_case

### Qt 規範（L2-21 ~ L2-25）

- L2-21：**提供給 QML 的名稱**（`Q_PROPERTY`、`Q_INVOKABLE`、供 QML 使用的 signal、model role）採 lowerCamelCase，
  以符合 QML 慣例；其餘 C++ 名稱依 L2-20 使用 snake_case
- L2-22：使用者設定以 `QSettings` 讀取並指定預設值；站點設定與使用者設定分開保存
- L2-23：計時器使用 `QTimer`，必須有父物件或以成員持有，並位於擁有者的執行緒
- L2-24：跨模組 `connect()` 只能出現在組裝根 `src/app/gcs_wiring.cpp`，並明確指定連線型別
- L2-25：使用帶函式指標的新式 `connect()` 語法，禁止 `SIGNAL()` / `SLOT()` 字串巨集

---

## L3 進階級規範（鼓勵遵守）

- L3-01：複雜物件傳 `const` 參考，簡單型別直接傳值
- L3-02：優先使用 `enum class`，不使用傳統 `enum`
- L3-03：使用 Qt logging category（`Q_LOGGING_CATEGORY`、`qCInfo` / `qCWarning` / `qCCritical`）替代 `std::cout` / `std::cerr`
- L3-04：動態記憶體 `delete` 後立即設為 `nullptr`；`QObject` 優先以父子關係管理生命週期
- L3-05：可調參數放入 `QSettings` 或站點設定，不寫死在程式碼中
- L3-06：遵守 DRY / KISS / YAGNI
- L3-07：標頭檔只宣告介面，實作放 `.cpp`（模板與 inline 例外）
- L3-08：不修改狀態的成員函式宣告為 `const`
- L3-09：函式單一職責，不超過 50 行
- L3-10：優先使用智慧指標（`unique_ptr` / `shared_ptr`）
- L3-11：Git 提交符合 `模組: 更改項目` 格式
- L3-12：核心模組只依賴 QtCore，不得 include QtGui、QtQml 或 QtQuick 標頭

---

## QML 規範

### 檔案與命名
- QML 元件檔使用 PascalCase，檔名即元件名（`FleetPanel.qml`）
- `id` 與自訂屬性使用 lowerCamelCase
- 每個 QML 檔開頭以註解說明用途（`@file`、`@brief`）

### 格式
- 4 空格縮排，單行不超過 120 字元
- 屬性順序：`id` → 自訂屬性與 signal → 尺寸與錨點 → 其他屬性 → 子元件 → 函式
- UI 文字與註解使用繁體中文，禁止中國用語；UI 文字以 `qsTr()` 包裝

### 狀態與通訊
- QML 只呈現狀態與收集操作，業務邏輯放在 C++
- QML 只能透過 `UiBridge` 與 models 取得資料、送出操作；禁止直接呼叫核心模組或 `IAgentLink`
- 命令按鈕的啟用狀態以 `UiBridge` 提供的屬性為準（由 Guard 轉移表推導），不得在 QML 自行重寫判斷
- Arm、Disarm、Takeoff、RTL、Land 必須具備 L1／L2 防呆確認

---

## 單元測試規範

- 所有單元測試程式碼集中在專案根目錄的 `test/` 資料夾。
- 禁止將單元測試程式碼放置在 `src/` 或其他主程式邏輯中；`src/` 內不得出現測試案例、測試框架或測試專用程式碼。
- 使用 Google Test，可搭配 QtTest 的 `QSignalSpy`；以 `gtest_discover_tests()` 註冊並透過 `ctest` 執行。
- 測試主程式需建立 `QCoreApplication`，以支援 queued connection 與 `QTimer`。
- 測試檔案命名使用 `test_<module>.cpp`，與被測模組對應。
- Guard 轉移表必須覆蓋每個 `FlightState` × `CmdType` 組合；組裝結果必須有無法繞過 Guard 的測試；
  DDS 型別需有 CDR 往返測試。
- 測試程式碼同樣遵守本文件的註解與命名規範（繁體中文、檔頭註解、英文識別符）。

---

## 程式碼生成附加要求

- 生成 C++ 與 QML 程式碼時，所有註解使用繁體中文
- 每個新函式必須加 Doxygen 註解（`@brief`、`@param`、`@return`）
- 每個新檔案必須加檔案頭註解（`@file`、`@brief`、`@author`、`@date`）
- 變數與函式命名使用英文，註解使用繁體中文
- 傳送控制指令前必須檢查 NaN / Inf
- 指標使用前必須檢查 `nullptr`
- 浮點比較必須使用 epsilon
- DDS 回呼函式名以 `_cb` 結尾
- 介面類別以 `I` 開頭
- 類別成員以 `_` 結尾，全域變數以 `g_` 開頭，布林變數以 `is_` / `has_` 開頭，常數全大寫蛇形
- 互斥鎖使用 `std::lock_guard`，不手動 `lock()` / `unlock()`
- DDS reader / writer 以類別成員持有
