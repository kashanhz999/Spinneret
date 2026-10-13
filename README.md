# Spinneret

**XML processing middleware for airport integration**, written in C++20.

Spinneret sits between an Airport Operational Database (AODB) and downstream airport systems. It takes in YIAPL/IATA AIDX XML messages, converts the **whole XML document to JSON without changing anything**, and publishes canonical events to IBM MQ queues and REST webhooks.

Spinneret is a C++ rewrite of the Java/Spring Boot service **SXPM**. Its output is identical to SXPM's, so existing consumers keep working unchanged. Several operational problems in SXPM are fixed; see [Differences from SXPM](#differences-from-sxpm).

---

## Supported messages

| Family | Purpose | Subtypes |
|---|---|---|
| `MSTR` | Master/reference data (airports, airlines, gates, stands…) | 9 |
| `FLSH` | Flight schedules (`DALY`, `ADHC`) | 2 |
| `FLTU` | Real-time flight updates (gate, stand, times, delays…) | 38 |
| `HLTH` | Health-check heartbeats | 2 |

The complete list is in [message_types.cpp](src/spinneret/domain/message_types.cpp).

## How a message is processed

```
 IBM MQ queue ─┐                                                        ┌─▶ IBM MQ queue
               ├─▶ 1 Header ─▶ 2 Intent ─▶ 3 XML→JSON ─▶ 4 Transform ─▶ ├
 POST /api/ingest                              5 Correlate · Publish · 6 Audit └─▶ Webhook (POST)
```

1. **Header:** parses the XML once and reads `YIAPL_Header`: MessageType, MessageSubType, SequenceNumber and so on.
2. **Intent:** maps the type and subtype to a business intent, e.g. `FLTU/FGAT` → `FLIGHT_GATE_UPDATE`.
3. **XML → JSON:** converts the full tree:
   - namespace prefix → `__prefix`
   - attributes → `_<name>`
   - text and CDATA → `__text`
   - repeated child elements → arrays
   - document order is kept
4. **Transform:** builds one event per message with a business key, e.g. `I51569_2024-05-26` (airline + flight number + origin date).
5. **Correlate and publish:** every log line carries the trace ID. The event goes to every enabled output.
6. **Audit:** the message's full journey is logged.

The event each output receives:

```json
{
  "eventId": "…", "traceId": "…",
  "eventType": "FLIGHT_GATE_UPDATE",
  "businessKey": "I51569_2024-05-26",
  "eventTime": "2026-10-13T09:40:24.799Z",
  "payloadJson": { "YIAPL_FlightLegNotifRQ": { "__prefix": "yiapl", "YIAPL_Header": { … } } }
}
```

### Failures

| Kind | Example | REST status | Retried? |
|---|---|---|---|
| `INVALID_XML` | malformed or empty | 400 | no (permanent) |
| `UNKNOWN_MESSAGE_TYPE` | unsupported type or subtype | 422 | no (permanent) |
| `TRANSFORM_FAILED` | transformer error | 500 | no (permanent) |
| `PUBLISH_FAILED` | a *required* output rejected the event | 502 | yes (transient) |
| `INTERNAL` | unexpected error | 500 | yes (transient) |

Each failed message produces one `PIPELINE_ERROR` event. Its payload holds the error and the original XML, pretty-printed.

For **IBM MQ input**, each message is processed inside a unit of work:
- **Transient failure:** the message is rolled back and redelivered after a short pause, up to the backout threshold.
- **Permanent failure or last attempt:** the message is moved to the backout queue (the queue's `BOQNAME`, or `adapters.input.ibmmq.backoutQueue`) and committed.

So one bad message can no longer block the queue or loop forever.

---

## Building

You need a C++20 compiler (GCC 13+, Clang 16+ or MSVC 19.38+), CMake 3.24+, pugixml, nlohmann-json, and GoogleTest for the tests. cpp-httplib is downloaded automatically at a pinned version.

**Windows (MSYS2 MinGW64 shell):**

```bash
pacman -S mingw-w64-x86_64-{cmake,ninja,gcc,pugixml,nlohmann-json,gtest,openssl}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build        # or ./build/spinneret_tests
```

**Linux (Debian/Ubuntu):**

```bash
sudo apt install build-essential cmake ninja-build libpugixml-dev nlohmann-json3-dev libgtest-dev libssl-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

### Build options

| Option | Default | Meaning |
|---|---|---|
| `SPINNERET_WITH_IBMMQ` | `OFF` | Build the IBM MQ adapters. Requires the IBM MQ client SDK. |
| `SPINNERET_MQ_HOME` | — | MQ client install directory, if not in the standard location |
| `SPINNERET_WITH_TLS` | `ON` | Allow `https://` webhook URLs when OpenSSL is found |
| `SPINNERET_BUILD_TESTS` | `ON` | Build `spinneret_tests` |

To enable IBM MQ, install the [IBM MQ redistributable client](https://www.ibm.com/support/pages/downloading-ibm-mq-clients), then build with:

```bash
cmake -S . -B build -DSPINNERET_WITH_IBMMQ=ON -DSPINNERET_MQ_HOME="C:/Program Files/IBM/MQ"
```

The real MQ client is only used in [mqi_session.cpp](src/spinneret/adapters/mq/mqi_session.cpp). Everything else, including the retry and backout logic, is tested with fake sessions and builds without the SDK. A build without MQ support reports that clearly if MQ is enabled in the configuration.

---

## Running

```bash
./build/spinneret --config-dir config --port 8080
```

On first start, Spinneret creates an API key and prints it **once**. Create named keys and revoke the bootstrap key:

```bash
spinneret keys create "aodb-feed"     # prints the key once
spinneret keys list
spinneret keys revoke <id>
```

Run the pipeline offline on a file. This is a dry run that prints the full diagnostic report:

```bash
spinneret process "tests/samples/FLTU/FGAT/Flight Gate Update FGAT.xml"
```

| Option | Environment variable | Default |
|---|---|---|
| `--config-dir` | `SPINNERET_CONFIG_DIR` | `config` |
| `--host` | `SPINNERET_HOST` | `0.0.0.0` |
| `--port` | `SPINNERET_PORT` | `8080` |
| `--log-level` | `SPINNERET_LOG_LEVEL` | `info` |
| — | `SPINNERET_MQ_PASSWORD` | overrides `ibm.mq.password` |

---

## Configuration

Runtime settings are stored in `<config-dir>/runtime-config.json`. The file holds only the values you have changed from the defaults. Change settings through the admin API: changes take effect immediately, and only the affected adapters restart. Unknown keys and wrong types are rejected.

| Key | Default |
|---|---|
| `adapters.input.rest.enabled` | `true` |
| `adapters.input.ibmmq.enabled` | `false` |
| `adapters.input.ibmmq.queue` | `AODB.FLIGHT.UPDATE` |
| `adapters.input.ibmmq.backoutThreshold` | `0` (use the queue's `BOTHRESH`, else 3) |
| `adapters.input.ibmmq.backoutQueue` | `""` (use the queue's `BOQNAME`) |
| `adapters.output.rest.enabled` | `false` |
| `adapters.output.rest.url` | `http://localhost:3000/api/v1/webhook` |
| `adapters.output.rest.required` | `false` |
| `adapters.output.rest.timeoutMs` | `5000` |
| `adapters.output.ibmmq.enabled` | `false` |
| `adapters.output.ibmmq.queue` | `AIRPORT.EVENTS.CANONICAL` |
| `adapters.output.ibmmq.required` | `true` |
| `ibm.mq.queueManager` / `channel` / `connName` | `QM1` / `DEV.ADMIN.SVRCONN` / `localhost(1414)` |
| `ibm.mq.user` / `password` | empty |
| `ibm.mq.sslCipherSpec` / `sslKeyRepository` | empty |

**Required vs optional outputs:**
- If a *required* output fails, the message fails: REST callers get a 502, and MQ input rolls the message back for redelivery.
- If an *optional* output fails, the failure is reported in the response, the logs and the metrics, but the message still succeeds.

**Templates:** `config/templates/*.json` contains `development`, `production` and `rest-only`. Each applies a bundle of keys in one call.

---

## HTTP API

The OpenAPI document is at `/openapi.json` and Swagger UI at `/docs`. Every `/api/**` endpoint requires an `X-API-Key` header.

| Method | Path | Purpose |
|---|---|---|
| POST | `/api/ingest` | Process a message (synchronous). Optional `X-Trace-Id` header. |
| POST | `/api/test/ingest[?publish=true]` | Diagnostic report. Dry run unless `publish=true`. |
| GET / PUT | `/api/admin/config` | Read (secrets masked) or update the configuration |
| POST | `/api/admin/config/restore` | Swap the configuration with its backup |
| GET | `/api/admin/config/templates` | List templates |
| POST | `/api/admin/config/template/{name}` | Apply a template |
| GET | `/api/admin/adapters` | Adapter inventory and health |
| POST | `/api/admin/adapters/{name}/start` \| `/stop` | Input adapter lifecycle |
| GET | `/health`, `/health/live` | Readiness (503 if an enabled adapter is unhealthy) and liveness |
| GET | `/metrics` | Prometheus metrics |
| GET | `/info` | Version and build features |

```bash
curl -X POST localhost:8080/api/ingest -H "X-API-Key: $KEY" -H "Content-Type: application/xml" \
     --data-binary @"tests/samples/FLTU/FGAT/Flight Gate Update FGAT.xml"

curl -X PUT localhost:8080/api/admin/config -H "X-API-Key: $KEY" -H "Content-Type: application/json" \
     -d '{"adapters.output.rest.enabled": true}'
```

---

## Differences from SXPM

**Unchanged:**
- Event JSON format
- XML → JSON conventions
- Intents and business keys
- Default queue names
- `XPM_TRACE_ID` (MQ property) and `X-Trace-Id` (webhook header)
- Admin endpoint paths

**Changed:**

| Area | SXPM | Spinneret |
|---|---|---|
| Unknown message type | `200 ACCEPTED`, nothing published | `422` plus a `PIPELINE_ERROR` event |
| `/api/ingest` docs | claimed asynchronous | synchronous and documented as such; real status codes |
| `/api/test/ingest` | unauthenticated, published to real outputs | API key required; dry run by default |
| MQ poison messages | redelivered forever, one error event per attempt | bounded retries, backout queue, one error event |
| MQ output failures | swallowed in a background task | reported; fail the message when the output is required |
| Webhook failures | logged only | reported in the response and metrics; can be made required |
| API keys | plain text in a committed file | SHA-256 hashes in a git-ignored file; CLI to manage them |
| `ibm.mq.*` changes at runtime | ignored (connection built once at startup) | MQ input and output reconnect with the new settings |
| `application.yml` adapter flags | silently unused | one settings source with documented defaults |
| Config updates | any key, any type | unknown keys and wrong types rejected; atomic writes |
| Template names | `../` could read other config files | restricted to `[A-Za-z0-9_-]` |
| XML parser | JDK DOM (external entities enabled by default) | pugixml (no external entity resolution) |
| Admin "start" of a disabled adapter | `200`, nothing happened | `409` with an explanation |
| Health | Actuator (`/actuator/health`) | `/health`, `/health/live`, `/metrics` |

---

## Project layout

```
src/spinneret/
  domain/      Message families and intents, event, header, correlation
  pipeline/    Stages, transformers, engine, diagnostics
  adapters/    in/ (REST, IBM MQ) · out/ (webhook, IBM MQ) · mq/ (session interface + MQI)
  config/      Runtime configuration and templates
  security/    API keys
  http/        Server, admin and operational routes
  lifecycle/   Restarting adapters after config changes
  metrics/     Prometheus counters and histogram
  app/         Wiring
tests/         GoogleTest suite and the SXPM sample corpus (122 XML files)
resources/     OpenAPI document (embedded at build time)
```

## Status

Version 0.1.0.
- **Tested:** the REST path, pipeline, configuration, API keys and MQ retry/backout logic, through 191 automated tests.
- **Not yet tested:** the IBM MQ client code (`mqi_session.cpp`) has only been syntax-checked. It must be built with `SPINNERET_WITH_IBMMQ=ON` and tested against a queue manager before MQ is used in production.
