#include <library/cpp/testing/unittest/registar.h>
#include <smart_devices/libs/unbound/unbound_enabler.h>

namespace {
    const std::string cfgJsSource = R"json(
{
    "enable": true,
    "checkHosts": [
      "quasar.yandex.net",
      "uniproxy.alice.yandex.net",
      "scbh.yandex.net"
    ],
    "serverParams": [
      "verbosity: 1",
      "msg-cache-size: 2m",
      "rrset-cache-size: 2m",
      "username: \"nobody\"",
      "chroot: \"\"",
      "serve-expired: yes",
      "serve-expired-ttl: 900",
      "use-syslog: no",
      "logfile: \"/data/quasar/daemons_logs/unbound.log\"",
      "statistics-interval: 1800"
    ],
    "forwarders": ["77.88.8.1","77.88.8.8"],
    "zones": {
      "yandex.net": {
        "quasar.yandex.net": "213.180.193.230",
        "uniproxy.alice.yandex.net": ["213.180.193.76", "213.180.204.80"],
        "events.wsproxy.alice.yandex.net": "213.180.193.140",
        "storage.mds.yandex.net": "213.180.204.158"
      },
      "yandex.ru": {
        "log.strm.yandex.ru": "87.250.251.15"
      }
    }
})json";
} // namespace

Y_UNIT_TEST_SUITE(UnboundEnabler) {
    Y_UNIT_TEST(parseCfg) {
        UnboundEnabler::Config cfg;
        auto isGoodCfg = UnboundEnabler::parseConfig(cfgJsSource, cfg);
        UNIT_ASSERT(isGoodCfg);
        UNIT_ASSERT_EQUAL(cfg.hostsToResolve.size(), 3);
        UNIT_ASSERT_EQUAL(cfg.forwarders.size(), 2);
        UNIT_ASSERT_EQUAL(cfg.zones.size(), 2);
        UNIT_ASSERT_EQUAL(cfg.zones.at("yandex.net").at("uniproxy.alice.yandex.net").size(), 2);
        UNIT_ASSERT_EQUAL(cfg.zones.at("yandex.net").at("quasar.yandex.net").at(0), "213.180.193.230");
    }
}
