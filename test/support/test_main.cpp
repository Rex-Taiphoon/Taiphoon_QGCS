/**
 * @file test_main.cpp
 * @brief Qt 模組測試共用的 Google Test 主程式
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * QTimer 與 queued connection 需要 QCoreApplication 與事件迴圈，因此不使用 gtest_main。
 */
#include "core/agent_types.hpp"

#include <QCoreApplication>

#include <gtest/gtest.h>

/**
 * @brief 測試程式入口
 * @param argc 參數數量
 * @param argv 參數內容
 * @return 測試結果
 */
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    gcs::register_core_metatypes();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
