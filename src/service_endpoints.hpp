// Служебные пути без аутентификации (спека docs-service-ops, «Служебные
// ручки»): /healthz, /version, /metrics, /openapi.yaml и /docs (Swagger UI
// из вендоренного бандла third_party/swagger-ui/, без CDN).
//
// openapi.yaml и swagger-ui читаются с диска по относительному пути (рабочая
// директория — рядом с исполняемым файлом): CMakeLists.txt копирует их туда
// при сборке (POST_BUILD), Containerfile — при сборке образа.
#pragma once

#include "httplib.h"

namespace endpoints {

void register_routes(httplib::Server& svr);

} // namespace endpoints
