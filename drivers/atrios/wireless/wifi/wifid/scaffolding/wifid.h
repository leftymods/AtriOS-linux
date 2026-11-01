#pragma once

#include <yandex_io/scaffolding/base/endpoint.h>
#include <yandex_io/scaffolding/base/entry.h>

int wifid(QuasarCallParams& params);

void addWifiServices(quasar::YandexIOEndpoint& yandexIOEndpoint, QuasarCallParams& params);
