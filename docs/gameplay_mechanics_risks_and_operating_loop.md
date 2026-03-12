# Gameplay Mechanics Risks And Operating Loop

This document identifies the key risks to the gameplay mechanics system and defines
the daily operating loop that the agent should follow to maintain progress, catch
problems early, and keep the work aligned with ZPC principles.

## Purpose

Risk identification prevents surprises. The operating loop prevents drift. Together
they ensure that the gameplay mechanics work stays productive, aligned, and
recoverable.

## Risk Register

### Risk 1: Premature Abstraction

Description: building overly general gameplay frameworks before enough concrete
mechanics exist to validate the abstractions.

Likelihood: high

Impact: high — wasted work, wrong interfaces, rework cost

Mitigation:

- implement the smallest working version of each system first
- require at least two concrete use cases before generalizing an interface
- review abstractions against the "would this survive a genre change" test
- defer framework generalization until Stage 3 or Stage 4

Detection:

- interfaces with no concrete implementation
- generic types with only one instantiation
- configuration surfaces that no scenario uses

### Risk 2: ZPC Divergence

Description: the gameplay branch diverges from ZPC master far enough that merging
becomes painful or impossible.

Likelihood: medium

Impact: high — the entire branch could become unmergeable

Mitigation:

- sync from master regularly (at least weekly)
- keep gameplay code in new files and new CMake targets rather than modifying
  existing ZPC core files wherever possible
- resolve merge conflicts immediately when they appear
- do not fork ZPC internal interfaces; extend them through the existing service
  and validation patterns

Detection:

- merge from master produces conflicts in core files
- gameplay code depends on modified ZPC internals that master does not have
- gameplay tests fail after master sync

### Risk 3: Scope Creep Into Engine Territory

Description: the gameplay mechanics system grows into a full game engine, absorbing
rendering, physics, networking, audio, and UI responsibilities that belong in other
layers.

Likelihood: medium

Impact: high — architectural boundary violation, maintenance burden

Mitigation:

- maintain strict layer boundaries: gameplay consumes services, it does not own
  rendering or physics
- require explicit justification for any gameplay code that imports rendering,
  physics, or networking headers
- keep the gameplay layer testable without a running renderer or physics engine
- follow the principle from the agent prompt: only add general capabilities to ZPC
  if they are genuinely missing

Detection:

- gameplay targets linking against rendering or physics targets
- gameplay tests requiring GPU or display initialization
- gameplay code directly managing windows, audio devices, or network sockets

### Risk 4: Validation Gaps

Description: gameplay systems ship without adequate tests, benchmarks, or canary
scenarios, making regressions invisible.

Likelihood: medium

Impact: medium — silent correctness or performance regressions

Mitigation:

- enforce the rule: no milestone is complete without tests and benchmark baselines
- require canary scenarios for every major system
- run validation comparison against baselines on every commit
- track benchmark trends over time

Detection:

- milestones marked complete without test coverage
- benchmark baselines that have not been updated after significant changes
- canary scenarios that always pass (they may be too weak)

### Risk 5: Data Format Lock-In

Description: the mechanics data format becomes rigid too early, making schema
evolution expensive when real content arrives.

Likelihood: medium

Impact: medium — migration cost, content breakage

Mitigation:

- version all data schemas from the beginning
- build migration support into the data loading pipeline early
- keep data formats simple and flat until complexity is justified
- use ZPC validation schemas which already support structured comparison

Detection:

- data format changes that break existing test data
- no version field in serialized mechanics data
- migration code that is untested or nonexistent

### Risk 6: Performance Regression Accumulation

Description: many small performance regressions individually pass threshold checks
but accumulate into a significant slowdown over time.

Likelihood: medium

Impact: medium — gradual performance degradation

Mitigation:

- track benchmark trends across all milestones, not just point-in-time comparisons
- periodically re-baseline from a known-good reference point rather than only
  comparing against the most recent baseline
- include scenario-level frame-budget benchmarks that catch aggregate regression
  even when individual operations pass

Detection:

- scenario-level benchmarks approaching frame budget limits
- monotonic upward trend in per-operation costs across multiple milestones
- baseline promotions that consistently relax thresholds

### Risk 7: Insufficient Research Before Implementation

Description: implementation proceeds without adequate research, leading to designs
that miss well-known patterns or repeat known mistakes.

Likelihood: low-medium

Impact: medium — suboptimal designs, rework

Mitigation:

- require research recommendations before each implementation stage
- survey at least two existing implementations or references per research area
- document tradeoffs and alternatives considered, not just the chosen approach
- the research roadmap is a living document; update it as findings arrive

Detection:

- implementation decisions with no corresponding research recommendation
- designs that reinvent patterns available in surveyed references
- surprise discoveries of existing solutions during implementation

## Operating Loop

### Daily Loop

The agent should follow this loop every session:

1. **Sync check**: verify whether ZPC master has updates. If it does, merge into
   the gameplay branch and resolve any conflicts before proceeding with new work.

2. **Status review**: check the milestone document for the current milestone, its
   acceptance criteria, and what remains to be done.

3. **Gap inventory**: identify the most impactful gap in the current milestone:
   missing implementation, missing test, missing benchmark, missing documentation,
   or missing research.

4. **Prioritize**: choose the single highest-value task. Prefer:
   - foundation work over extension work
   - tests and benchmarks over new features
   - fixing broken things over building new things
   - research for upcoming stages over premature implementation

5. **Implement**: do the work. Pair every new capability with:
   - at least one unit test
   - validation schema integration where applicable
   - a benchmark scenario if the capability is performance-relevant
   - documentation update

6. **Verify**: run tests, benchmarks, and canary scenarios. Compare against
   baselines. Fix regressions before moving on.

7. **Commit and push**: commit the work with a clear message. Push to the remote.

8. **Record next target**: update the milestone document or this operating loop
   with the next session's minimum target.

### Weekly Loop

Once per week (or every 5-7 sessions):

1. **Master sync**: ensure the gameplay branch is up to date with master.

2. **Milestone review**: assess whether the current milestone is on track. If
   acceptance criteria are met, close the milestone and advance to the next one.

3. **Risk review**: scan the risk register. Has any risk materialized? Has any new
   risk appeared? Update the register.

4. **Benchmark trend review**: compare current benchmark results against the
   earliest baselines, not just the most recent ones. Look for accumulating
   regression.

5. **Research gap review**: check whether upcoming implementation stages have
   adequate research coverage. If not, schedule research before implementation
   begins.

6. **Documentation review**: verify that all completed work has corresponding
   documentation. Fill gaps.

### Stage Transition Loop

When completing a major implementation stage:

1. **Full verification**: run all tests, all benchmarks, all canary scenarios.
   Everything must pass.

2. **Baseline promotion**: promote benchmark baselines for the completed stage.

3. **Documentation audit**: verify that all public interfaces from the stage are
   documented.

4. **Cross-reference check**: verify that the research roadmap, implementation
   roadmap, benchmark plan, and milestones are all consistent and up to date.

5. **Merge assessment**: evaluate whether the current branch state is merge-ready
   or needs further work before merging to master.

6. **Next stage planning**: review the research roadmap for the next stage. Ensure
   research recommendations exist before starting implementation.

## Recovery Procedures

### If tests fail after master sync

1. Identify whether the failure is in gameplay code or in changed ZPC core code.
2. If gameplay code: fix the gameplay code to work with the updated ZPC interfaces.
3. If ZPC core code: determine whether the ZPC change is intentional. If so, adapt.
   If not, report the issue.
4. Do not proceed with new work until all tests pass.

### If benchmarks regress

1. Identify which change caused the regression.
2. If the regression is in gameplay code: optimize or revert.
3. If the regression is in ZPC core code after a master sync: report and document.
4. If the regression is acceptable (intentional tradeoff): promote a new baseline
   with documented justification.

### If a milestone is blocked

1. Identify the blocking dependency.
2. If the dependency is in ZPC core: determine whether it can be added to ZPC or
   whether the milestone needs to be redesigned.
3. If the dependency is in a prerequisite milestone: go back and complete the
   prerequisite.
4. Document the blockage and the resolution plan.

### If scope creep is detected

1. Identify the code or design that crosses a layer boundary.
2. Move it to the correct layer or remove it.
3. Add a note to the risk register about the specific creep pattern.
4. Review whether the operating loop needs adjustment to prevent recurrence.

## Success Criteria For The Operating Loop

The operating loop is working correctly when:

- every session produces at least one committed, tested, documented change
- no milestone stays in progress for more than the estimated duration without a
  documented reason
- benchmark trends are stable or improving
- master sync is never more than one week behind
- the risk register is reviewed and current
- the agent can always state what the next session's minimum target is

## Related Documents

- [gameplay_mechanics_agent_prompt.md](gameplay_mechanics_agent_prompt.md)
- [gameplay_mechanics_research_roadmap.md](gameplay_mechanics_research_roadmap.md)
- [gameplay_mechanics_implementation_roadmap.md](gameplay_mechanics_implementation_roadmap.md)
- [gameplay_mechanics_benchmark_plan.md](gameplay_mechanics_benchmark_plan.md)
- [gameplay_mechanics_milestones.md](gameplay_mechanics_milestones.md)
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)
- [roadmap.md](roadmap.md)
