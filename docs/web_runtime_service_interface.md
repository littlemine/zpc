# Web Runtime Service Interface

ZPC needs a web-facing interface story that is more concrete than "WASM later".
The important question is how browser clients, web applications, and service-backed
UIs interact with a ZPC-based runtime without collapsing the runtime boundary into
ad hoc HTTP handlers or browser-specific shortcuts.

## Why This Page Exists

This page defines how a web-based interface should talk to a ZPC runtime or a
ZPC-backed service. It is the interface-design page for browser-facing and
service-mediated delivery.

## Core Design Goal

The web surface should feel like a product interface, not like a debug backdoor.

That means:

- browser clients interact through stable runtime or service contracts
- the deployable runtime ABI remains the authoritative low-level execution boundary
- transport and session concerns stay above the runtime core
- web applications and WASM clients can degrade cleanly when a capability is not
  available locally

## Two Interaction Modes

### Mode 1: Local WASM Runtime Consumer

In this model, a browser-hosted or desktop-embedded web UI interacts with a
WASM-oriented ZPC packaging layer.

Characteristics:

- selected runtime features are compiled into a browser-safe package
- filesystem, threading, and artifact-loading assumptions are sandbox-aware
- execution happens in-process with the web app shell
- capabilities are narrower than native full-dev or desktop profiles

This is appropriate for lightweight inspection, authoring, validation playback,
small-scale local evaluation, and carefully scoped interactive tools.

### Mode 2: Web Client To Runtime Service

In this model, the browser speaks to a service that owns a real ZPC runtime.

Characteristics:

- the browser is a remote control and visualization client
- scheduling, resources, queues, and heavy artifacts stay server-side
- transport can be HTTP, WebSocket, gRPC-web-style, or another service protocol
- runtime capabilities can exceed what a local WASM package could support

This is the right default for heavier compute, backend-specific execution,
multi-user tooling, persistent resources, and long-running workflows.

## Unconstrained Research Space

If ZPC were designed without assuming a single product or deployment style, the web
surface could reasonably take several shapes.

### Shape 1: Browser-As-Viewer

The browser only visualizes and controls runs hosted elsewhere.

Strengths:

- simplest security model for expensive or privileged backends
- easiest way to support CUDA, Vulkan, clusters, and persistent assets
- cleanest place to centralize validation history and benchmark baselines

Weaknesses:

- higher latency for interactive authoring
- reduced offline capability
- risk that the browser becomes a thin shell over an opaque server

### Shape 2: Browser-As-Lightweight-Executor

The browser executes a constrained local runtime through WASM and web APIs.

Strengths:

- immediate local feedback
- good fit for educational, inspection, or lightweight authoring tools
- useful for offline or sandbox-friendly workflows

Weaknesses:

- constrained threading, memory, and file access
- difficult parity with native runtime capabilities
- risk of designing around browser limits instead of the broader platform

### Shape 3: Hybrid Local And Remote Execution

The browser can switch between local preview and remote production execution.

Strengths:

- best product ergonomics when it works well
- can use local evaluation for quick feedback and remote execution for heavy runs
- naturally supports canary preview versus full benchmark or validation runs

Weaknesses:

- highest semantic complexity
- capability differences must be explicit, not hidden
- harder cache, artifact, and result-consistency story

For ZPC, the hybrid model is likely the best long-term product shape, but the
service-backed model is still the safest first implementation.

## Contract Layers

The interface stack should stay layered.

### Layer A: Stable Runtime Boundary

The runtime ABI remains the low-level compatibility anchor for engine ownership,
submission, cancellation, status, extension query, and validation export.

The web stack should not bypass this boundary by reaching into unstable C++ types.

### Layer B: Service Or Session API

Above the runtime ABI, a web-oriented service contract should define concepts such
as:

- session creation and teardown
- capability discovery
- resource registration and lookup
- task or graph submission requests
- validation, benchmark, and event-stream retrieval
- artifact upload, packaging, and deployment handles

This layer is transport-friendly and user-facing.

### Layer C: Web Application Model

The browser-facing application then adds:

- authentication and multi-user state where needed
- view models and incremental updates
- authoring workflows
- rendering, plots, logs, and inspection panels
- human-tuned parameter editing and scenario management

That layer belongs in the web application, not in the runtime core.

## Required Interface Concepts

The web layer should expose stable, inspectable concepts rather than opaque action
buttons.

### Capability Discovery

The client should be able to query:

- available profiles
- enabled backends
- runtime limits and queue or executor capabilities
- validation and reporting surfaces
- whether local or remote execution is active

### Session And Resource Ownership

A browser client must not infer ownership from process state. Explicit session and
resource handles are required.

That includes:

- session id or connection-scoped context
- resource handles with lifecycle state
- submission identifiers and event identifiers
- artifact identifiers for uploaded or compiled content

### Event And Report Streaming

The web interface should support incremental rather than snapshot-only interaction.

Useful streams include:

- submission lifecycle updates
- validation summary and report publication
- benchmark progress and completion
- resource maintenance or invalidation events
- log and diagnostics channels

### Human-Editable Parameters

If the web interface is intended for tuning, the contract has to model parameter
schemas, accepted ranges, units, and validation states rather than leaving the UI to
guess.

## Recommended Service Shape

The web-service layer should converge on a few coarse service groups.

### Runtime Control Service

- create or close session
- inspect capabilities
- submit work
- cancel or suspend work
- query status and results

### Resource Service

- register or update runtime-owned resources
- inspect resource state
- request maintenance or retirement
- acquire or release short-lived access tokens where appropriate

### Validation And Benchmark Service

- retrieve latest validation summary
- stream deterministic JSON or text reports
- fetch benchmark artifacts and baselines
- compare runs against stored canaries

### Authoring And Scenario Service

- enumerate canary scenarios
- push parameter changes
- save or load authored configurations
- run evaluation sessions against the runtime

## Topology Options

The service model itself can be deployed in multiple ways.

### Topology A: Single-Process Embedded Service

The app hosts both UI and runtime-facing services in one process.

Best for:

- local native apps embedding a web view
- low-latency editor tooling
- early development and debugging

Main risk:

- easy to blur the line between service contract and internal implementation

### Topology B: Local Sidecar Service

The browser or native UI talks to a separately managed local process.

Best for:

- desktop developer workflows
- isolating crashes and backend dependencies from the UI shell
- keeping the product model close to future remote deployments

Main risk:

- extra process management and packaging complexity

### Topology C: Remote Multi-Tenant Service

The browser talks to a shared remote deployment.

Best for:

- team workflows
- persistent validation or benchmark history
- centralized artifact storage and backend orchestration

Main risk:

- auth, quota, tenant isolation, and scheduling fairness stop being optional

The safest architectural rule is to make the session and service model work for all
three topologies, even if only one is implemented first.

## Transport Options And Tradeoffs

The transport decision should follow interaction shape, not fashion.

### Request-Response HTTP

Best for:

- session setup
- static artifact lookup
- durable result retrieval
- admin and packaging workflows

Weak for:

- live submission progress
- diagnostics streaming
- interactive tuning

### WebSocket Or Similar Bidirectional Streams

Best for:

- live runtime status
- validation event streams
- interactive scenario tuning
- log and progress channels

Weak for:

- cache-friendly bulk retrieval and simple operations where HTTP is enough

### gRPC-Style Contracts

Best for:

- strict schema evolution
- typed streaming interfaces
- internal service-to-service contracts

Weak for:

- browser-first simplicity if the stack becomes too infrastructure-heavy too early

The likely practical answer is mixed transport: HTTP for durable resources and
control-plane setup, streaming transport for live status and interaction.

## State Ownership Model

The biggest architectural question is where durable state lives.

### Server-Owned State

The service owns sessions, artifacts, validation history, and scenario instances.

This is best for:

- reproducibility
- collaboration
- persistent benchmark and canary comparison

### Client-Owned Draft State

The browser owns temporary edits, local parameter experiments, and unsaved drafts.

This is best for:

- responsiveness
- optimistic editing
- avoiding service chatter on every UI change

The healthy split is usually:

- durable execution and artifact state on the service
- ephemeral UI draft state in the client

## Failure Modes To Design For

The web model should assume that at least some of these happen routinely:

- sessions expire while the UI is open
- the active backend changes between capability query and execution
- local and remote execution produce slightly different available features
- large artifact upload succeeds while the follow-up run request fails
- validation or comparison reports arrive after the UI has moved on to another run

These are reasons to make all meaningful entities explicit and versioned: session
id, scenario id, submission id, artifact id, report id, and capability revision.

## Anti-Patterns

The web path should explicitly avoid:

- browser code reaching into unstable C++ object layout through special bindings
- one RPC endpoint per widget action with no coherent domain model
- mixing authentication, runtime submission, and UI state into one service blob
- pretending local WASM and remote native execution are interchangeable when they
  are not
- treating logs as the primary UI integration contract instead of typed reports and
  state snapshots

## Research Recommendation

The broad research answer is:

- define one typed session and capability model first
- assume hybrid local-preview and remote-execution as the long-term target
- implement the first production path as service-backed, not WASM-first
- treat the browser as a real product surface with typed reports, streaming, and
  explicit scenario schemas
- use local WASM execution only for use cases where sandbox constraints are a real
  advantage rather than a burden

## Candidate First Products

Promising first web-facing products include:

- validation report viewer with baseline comparison
- benchmark dashboard with downloadable artifacts
- remote canary tuning panel for scenario parameters
- lightweight graph or task submission console for known descriptors

## Browser And WASM Constraints

The interface must assume:

- restricted local filesystem access
- constrained threading and background execution models
- packaging and upload friction for heavy artifacts
- higher latency and partial connectivity in remote-service cases

Those constraints argue for explicit capability discovery and incremental status
updates instead of desktop-style hidden assumptions.

## Security And Isolation Consequences

A web-facing runtime service must isolate:

- tenant or user sessions
- artifact storage and execution rights
- resource namespaces
- validation and benchmark history visibility

That concern belongs at the service and deployment layer, not in the portable core,
but it has to shape the interface model from the beginning.

## Sequencing

The practical order is:

1. stabilize the runtime and validation-facing low-level contracts
2. define a session-oriented service API above those contracts
3. build one thin browser client against that service
4. add local WASM packaging only where the use case genuinely benefits from it

The service-first path is usually safer than forcing everything through local WASM
too early.

## Current Research Direction

The strongest long-term shape is a hybrid model with explicit capability reporting,
but the strongest immediate implementation direction is still service-first with
typed session, validation, resource, and scenario services.

## Current Design Consequence

The web story should be treated as an explicit runtime-consumer architecture with
its own session, capability, and reporting model. It should not be reduced to a UI
skin over unstable in-process internals.

## Related Pages

- [lightweight_frontend_integration.md](lightweight_frontend_integration.md) for
  frontend composition boundaries
- [plugin_and_abi_stability.md](plugin_and_abi_stability.md) for the stable
  deployable boundary behind the service model
- [async_runtime_abi.rst](async_runtime_abi.rst) for the concrete low-level ABI
  surface
- [application_layer_design.md](application_layer_design.md) for shared app-shell
  and UI service concerns
