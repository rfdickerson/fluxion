# Standards And Safety Profiles

Fluxion should not force every game, simulator, robot, rocket, or trading strategy to carry the same certification burden. The better design is an optional assurance-profile system: a module can opt into stricter language rules, runtime checks, evidence generation, and domain standards when the application needs them.

This document proposes the core mechanism and the first domain profiles.

## Reference Standards And Regulations

These are not copied into Fluxion. They are external anchors that Fluxion profiles can map to:

- NASA software engineering and assurance: [NPR 7150.2D resources](https://www.nasa.gov/intelligent-systems-division/software-management-office/nasa-software-engineering-procedural-requirements-standards-and-related-resources/), [NASA-STD-8739.8](https://standards.nasa.gov/standard/nasa/nasa-std-87398), and [SWE-134 safety-critical software requirements](https://swehb.nasa.gov/display/7150/SWE-134%2B-%2BSafety%2BCritical%2BSoftware%2BRequirements).
- Aviation software assurance: [FAA AC 20-115D](https://www.faa.gov/regulations_policies/advisory_circulars/index.cfm/go/document.information/documentID/1032046) and [RTCA DO-178C overview](https://www.rtca.org/do-178/), including tool qualification and formal-method supplements.
- Generic functional safety: [IEC 61508-1:2010](https://webstore.iec.ch/en/publication/5515), which covers E/E/PE safety-related systems and provides the basis for many sector standards.
- Automotive functional safety: [ISO 26262](https://www.iso.org/publication/PUB200262.html), especially the software and ASIL-oriented parts.
- Market-access and algorithmic-trading controls: [SEC Rule 15c3-5](https://www.sec.gov/rules-regulations/2011/06/risk-management-controls-brokers-or-dealers-market-access), [17 CFR 240.15c3-5](https://www.law.cornell.edu/cfr/text/17/240.15c3-5), [SEC market-access FAQ](https://www.sec.gov/rules-regulations/staff-guidance/trading-markets-frequently-asked-questions/divisionsmarketregfaq-0), [FINRA algorithmic trading guidance](https://www.finra.org/rules-guidance/key-topics/algorithmic-trading), and [ESMA MiFID II Article 17](https://www.esma.europa.eu/publications-and-data/interactive-single-rulebook/mifid-ii/article-17-algorithmic-trading).

## Core Mechanism

Fluxion should add three optional declarations:

```fluxion
profile FlightSafety inherits HardRealtime, SafetyCase
profile MarketAccess inherits UltraLowLatency, AuditReplay

module Guidance.Landing
  use profile FlightSafety

reactor DescentController
  assurance critical
  hazard HZ_LANDING_INSTABILITY
  safe_state EngineCutoff
{
  ...
}
```

The compiler treats profiles as constraints that expand into static checks, runtime checks, required metadata, and evidence outputs. Profiles are composable, but conflicts are compile errors.

## Proposed Profile Stack

| Profile | Purpose | Main added guarantees |
|---|---|---|
| `SoftRealtime` | Default engine/robotics profile | Bounded queues, no blocking realtime handlers, deterministic phases |
| `HardRealtime` | Deadline-sensitive control | no recursion, bounded loops, no unproven allocation, WCET evidence required |
| `SafetyCase` | Safety-critical systems | hazard traceability, safe states, command sequencing, input/output integrity checks |
| `FlightSafety` | Rocket/aircraft-style GNC | mode/state machines, independent inhibit/arm gates, restart-to-safe-state, strict evidence package |
| `FunctionalSafety` | IEC 61508/ISO 26262-style systems | SIL/ASIL metadata, safety functions, freedom-from-interference rules |
| `UltraLowLatency` | HFT/feed-handler hot paths | zero allocation, CPU affinity, fixed layout, deterministic timestamping |
| `MarketAccess` | Trading risk controls | pre-trade limits, order throttles, kill switches, audit/replay, authorization boundaries |
| `FormalSubset` | Proof-friendly core | total functions, restricted effects, finite-state reactors, proof obligations |
| `InteropQualified` | Safe FFI/codegen | extern allowlists, ABI layout reports, generated C/C++ rule packs |

## Static Checks By Profile

`HardRealtime`:

- every loop has a static bound or proven bounded iterator
- no recursion or dynamic dispatch in realtime handlers
- all allocations come from statically sized regions or pools
- every handler has WCET metadata or measured bound evidence
- every stream edge declares capacity, overflow policy, and backpressure behavior

`SafetyCase`:

- every safety-critical reactor has a `hazard` id
- every hazardous command passes through an explicit safety gate
- every safety-critical mode has a declared safe state
- off-nominal faults have bounded-response handlers
- input/output integrity checks are required at safety boundaries

`FlightSafety`:

- boot, restart, mode transition, and shutdown handlers must reach declared safe states
- commands that can create hazards require prerequisite checks and sequencing
- one software event cannot directly trigger a hazardous actuator command
- redundant sensors and voting logic are marked at the type/interface level
- flight-critical extern calls require qualification metadata

`FunctionalSafety`:

- safety functions are tagged with `sil` or `asil`
- mixed-criticality dataflow requires isolation or proven freedom from interference
- safety mechanisms have test hooks and diagnostic coverage metadata
- configuration and calibration values are versioned and range-checked

`MarketAccess`:

- every outbound order must pass through a `pre_trade_gate`
- price, size, duplicate, order-rate, position, credit/capital, and restricted-instrument checks are explicit
- kill switches are required for strategy, account, venue, and global levels
- generated audit logs are deterministic and replayable
- production strategies require supervision/authorization metadata

## Evidence Outputs

A profile build should produce a machine-readable evidence bundle:

```text
target/evidence/
  scheduler_graph.json
  stream_capacities.json
  wcet_claims.json
  region_memory_map.json
  hazard_traceability.json
  safety_gate_report.json
  ffi_abi_report.json
  replay_manifest.json
  generated_code_rules.json
```

This does not certify a system by itself. It gives engineers and independent reviewers concrete artifacts to connect Fluxion source code to the process required by the relevant domain.

## Language Features Needed

1. `profile` declarations with inheritance and conflict checks.
2. `assurance` levels on modules/reactors/functions: `standard`, `realtime`, `critical`.
3. `hazard`, `safe_state`, `requires`, and `inhibits` metadata.
4. A capability/effect system: `realtime`, `blocking`, `io`, `nondeterministic`, `unsafe`, `market_access`.
5. Bounded loop syntax: `for i in 0..N static`.
6. Safety gates as first-class reactors or command filters.
7. Evidence generation as part of the compiler contract.
8. Profile-specific standard library subsets.
9. Certified/qualified backend modes for generated C, C++, or native code.

## Design Rule

Profiles should constrain code without changing ordinary Fluxion semantics. A controller, strategy, or reactor should mean the same thing in every profile; stricter profiles simply reject more programs and require more evidence.
