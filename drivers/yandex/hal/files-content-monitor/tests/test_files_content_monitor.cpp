#include <smart_devices/libs/files_content_monitor/files_content_monitor.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>
#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/libs/base/persistent_file.h>
#include <yandex_io/tests/testlib/test_callback_queue.h>

#include <chrono>
#include <future>

using namespace std::chrono_literals;
using namespace quasar;

namespace {

    class Fixture: public NUnitTest::TBaseFixture {
    public:
        Fixture() = default;
        ~Fixture() {
            for (const auto& node : nodes_) {
                std::remove(node.path.c_str());
            }
        }

        void addNode(std::string nodeName, FilesContentMonitor::Node::MonitoringMode mode, std::string initialValue, std::chrono::milliseconds period = std::chrono::milliseconds{0}) {
            AtomicFile file(nameToPath(nodeName));
            file.write(initialValue);

            nodes_.push_back({.path = nameToPath(nodeName),
                              .name = nodeName,
                              .type = FilesContentMonitor::Node::Type::STRING,
                              .mode = mode,
                              .period = period});
        }

        void addNode(std::string nodeName, FilesContentMonitor::Node::MonitoringMode mode, int initialValue, std::chrono::milliseconds period = std::chrono::milliseconds{0}) {
            AtomicFile file(nameToPath(nodeName));
            file.write(std::to_string(initialValue));

            nodes_.push_back({.path = nameToPath(nodeName),
                              .name = nodeName,
                              .type = FilesContentMonitor::Node::Type::INT,
                              .mode = mode,
                              .period = period});
        }

        void createMonitor() {
            monitor = std::make_unique<FilesContentMonitor>(std::make_unique<TestCallbackQueue>());
            for (const auto& node : nodes_) {
                monitor->addNode(node);
            }
        }

        static std::string nameToPath(const std::string& name) {
            return std::string("test_path_") + name;
        }

    public:
        std::unique_ptr<FilesContentMonitor> monitor;

    protected:
        std::vector<FilesContentMonitor::Node> nodes_;
    };

    class MockListener: public FilesContentMonitor::IListener {
    public:
        MOCK_METHOD(void, onData, (const std::string&, const Json::Value&), (override));
    };

} /* anonymous namespace */

Y_UNIT_TEST_SUITE_F(FilesContentMonitor, Fixture) {
    Y_UNIT_TEST(testFilesContentMonitorInt) {
        addNode("file1", FilesContentMonitor::Node::MonitoringMode::ONCE, 2);
        Json::Value value;
        value["data"] = 2;
        auto listener1 = std::make_shared<MockListener>();
        {
            EXPECT_CALL(*listener1, onData("file1", value)).Times(1);
        }
        createMonitor();
        monitor->addListener(listener1);
    }

    Y_UNIT_TEST(testFilesContentMonitorString) {
        addNode("file1", FilesContentMonitor::Node::MonitoringMode::ONCE, "test_value_1");
        Json::Value value;
        value["data"] = "test_value_1";
        auto listener1 = std::make_shared<MockListener>();
        {
            EXPECT_CALL(*listener1, onData("file1", value)).Times(1);
        }
        createMonitor();
        monitor->addListener(listener1);
    }

    Y_UNIT_TEST(testFilesContentMonitorSeveralFiles) {
        addNode("file1", FilesContentMonitor::Node::MonitoringMode::ONCE, "test_value_1");
        addNode("file2", FilesContentMonitor::Node::MonitoringMode::ONCE, 5);
        Json::Value value1;
        value1["data"] = "test_value_1";

        Json::Value value2;
        value2["data"] = 5;

        auto listener1 = std::make_shared<MockListener>();
        {
            EXPECT_CALL(*listener1, onData("file1", value1)).Times(1);
            EXPECT_CALL(*listener1, onData("file2", value2)).Times(1);
        }

        createMonitor();
        monitor->addListener(listener1);
    }
}
