# Documentation Coverage Matrix

Status: complete

Legend: `planned`, `active`, `complete`, `not applicable`.

| Topic | Public page | API group | Kconfig | Generated input | Example | Architecture | Evidence | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| installation/version | getting started | version/core | base | none | first application | static system | smoke/host | complete |
| core and errors | concepts | core | diagnostics | none | first application | failure containment | core tests | complete |
| components/catalogs | concepts | component/catalog | descriptors | none | system composition | catalogs/binding | catalog tests | complete |
| System/Blueprint/binding | concepts/subsystem | system | strict binding | manifest binding | system composition | static system/normalization | system tests | complete |
| lifecycle | concepts/subsystem | lifecycle | capacities/timeouts | reports | system composition | lifecycle engine | lifecycle tests | complete |
| Kernel | subsystem | kernel | diagnostics/fatal | none | first application | Kernel/Zephyr | kernel tests | complete |
| execution | subsystem | execution | registrations/timeouts | none | system composition | execution runtime | execution tests | complete |
| hardware | subsystem | hardware | driver features | devicetree aliases | hardware fixtures | hardware generation | hardware tests | complete |
| Bus | subsystem | bus | capacity/policy | none | data pipeline | subsystem storage | Bus tests | complete |
| Parameters | subsystem | parameters | capacity/persistence | settings keys | data pipeline | subsystem storage | Parameters tests | complete |
| Events | subsystem | events | ingress/history/policy | none | data pipeline | subsystem storage | Events tests | complete |
| Metrics | subsystem | metrics | backend/capacity/policy | none | data pipeline | subsystem storage | Metrics tests | complete |
| Logging | subsystem | log | buffers/filtering | none | data pipeline | subsystem storage | Logging tests | complete |
| Remote | subsystem/tutorial | remote | service/protocol limits | manifest/client | remote control | Remote runtime | Remote tests/vectors | complete |
| Inspection | subsystem | inspection | pages/formats | Remote adapters | supervised device | subsystem storage | Inspection tests | complete |
| Health | subsystem | health | subjects/history | none | supervised device | health evidence | Health tests | complete |
| Supervisor | subsystem | supervisor | cadence/recovery | none | supervised device | supervision flow | Supervisor tests | complete |
| compatibility | reference | all aggregates | feature matrix | generator versions | all | boundaries | integration closure | complete |
| contributor workflow | development | extension points | test configs | tools | all | complete set | repository tests | complete |

## Required Global Checks

| Check | Status |
| --- | --- |
| warning-fatal HTML build | complete |
| Doxygen XML and Breathe integration | complete |
| internal cross-reference validation | complete |
| external link check | complete |
| canonical examples compile | complete |
| runnable native examples pass | complete |
| strict and relaxed binding represented | complete |
| Kconfig symbol coverage | complete |
| public aggregate/API coverage | complete |
| generated references deterministic | complete |
| fresh-user audit | complete |
| Remote host-path audit | complete |
| architecture trace audit | complete |
| deferred-feature claim audit | complete |
| responsive/readability audit | complete (structural; see handoff limit) |
