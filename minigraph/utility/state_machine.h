#pragma once
#ifndef MINIGRAPH_UTILITY_STATE_MACHINE_H_
#define MINIGRAPH_UTILITY_STATE_MACHINE_H_

#include "portability/sys_types.h"
#include <boost/sml.hpp>
#include <folly/AtomicHashMap.h>
#include <assert.h>
#include <iostream>
#include <map>
#include <memory>
#include <stdio.h>
#include <unordered_map>
#include <vector>

namespace minigraph {
namespace utility {

namespace sml = boost::sml;
using namespace sml;

// Define Events
struct Load {};
struct Unload {};
struct NothingChanged {};
struct Changed {};
struct Aggregate {};
struct Fixpoint {};
struct GoOn {};

// Define State Machine for each single graph.
struct GraphStateMachine {
  auto operator()() const {
    using namespace sml;
    front::event<Load> event_load;
    front::event<Unload> event_unload;
    front::event<NothingChanged> event_nothing_changed;
    front::event<Changed> event_changed;
    front::event<Aggregate> event_aggregate;
    front::event<Fixpoint> event_fix_point;
    front::event<GoOn> event_go_on;
    return make_transition_table(
        *"Idle"_s + event_load = "Active"_s, "Idle"_s + event_unload = "Idle"_s,
        "Active"_s + event_nothing_changed = "RT"_s,
        "Active"_s + event_changed = "RC"_s,
        "RC"_s + event_aggregate = "Idle"_s, "RT"_s + event_go_on = "Idle"_s,
        "RT"_s + event_fix_point = X);
  }
};

// State Machine definition for each graph.
struct SystemStateMachine {
  auto operator()() const {
    using namespace sml;
    front::event<GoOn> event_go_on;
    front::event<Fixpoint> event_fix_point;
    return make_transition_table(*"Run"_s + event_go_on = "Run"_s,
                                 "Run"_s + event_fix_point = X);
  }
};

// Class for state machine maintained in the system.
// It start from the begining of the system and destroyed when fixpoint is
// reached. At any point of time, a sub-graph is in one of five states:
// Active('A'), Idle('I'), Ready-to-Terminate, i.e RT ('R'),
// Ready-to-be-Collect, i.e RC ('C), and Terminate, i.e X('X').
// The transition from one state to another is triggered by an event.
// There are seven types of events for graph_state_: Load, Unload, ...,
// Fixpoint. and two types of events for system_state_: GoOn and Fixpoint.
//
// On ProcessEvent(GID_T gid, const char event), the state machine of gid
// changed according to transition table of GraphStateMachine.
//
// system_state_ changed from Run to X only if all state of graphs reach RT, i.e
// Fixpoint.
template <typename GID_T>
class StateMachine {
 public:
  StateMachine(const std::vector<GID_T>& vec_gid) {
    for (auto& iter : vec_gid) {
      graph_state_.insert(
          std::make_pair(iter, std::make_unique<sml::sm<GraphStateMachine>>()));
    }
  };
  StateMachine() {}

  ~StateMachine(){};

  void ShowGraphState(const GID_T& gid) const {
    auto iter = graph_state_.find(gid);
    if (iter != graph_state_.end()) {
      iter->second->visit_current_states(
          [](auto state) { std::cout << state.c_str() << std::endl; });
    } else {
    }
  };

  bool GraphIs(const GID_T& gid, const char& event) const {
    assert(event == IDLE || event == ACTIVE || event == RT || event == RC ||
           event == TERMINATE);
    auto iter = graph_state_.find(gid);
    using namespace sml;
    if (iter != graph_state_.end()) {
      switch (event) {
        case IDLE:
          return iter->second.is("Idle"_s);
        case ACTIVE:
          return iter->second.is("Active"_s);
        case RT:
          return iter->second.is("RT"_s);
        case RC:
          return iter->second.is("RC"_s);
        case TERMINATE:
          return iter->second.is(X);
        default:
          break;
      }
    }
    return false;
  };

  bool IsTerminated() {
    using namespace sml;
    unsigned count = 0;
    for (auto& iter : graph_state_) {
      iter.second->is("RT"_s) ? ++count : 0;
    }
    if (count < graph_state_.size()) {
      for (auto& iter : graph_state_) {
        iter.second->process_event(GoOn{});
        assert(iter.second->is("Idle"_s));
      }
      return false;
    } else {
      system_state_.process_event(Fixpoint{});
      assert(system_state_.is(X));
      return true;
    }
  };

  bool ProcessEvent(GID_T gid, const char event) {
    using namespace sml;
    std::cout << event << std::endl;
    assert(event == LOAD || event == UNLOAD || event == NOTHINGCHANGE ||
           event == CHANGED || event == AGGREGATE || event == FIXPOINT ||
           event == GOON);
    auto iter = graph_state_.find(gid);
    if (iter != graph_state_.end()) {
      switch (event) {
        case LOAD:
          iter->second->process_event(Load{});
          assert(iter->second->is("Active"_s));
          break;
        case UNLOAD:
          iter->second->process_event(Unload{});
          assert(iter->second->is("Idle"_s));
          break;
        case NOTHINGCHANGE:
          iter->second->process_event(NothingChanged{});
          assert(iter->second->is("RT"_s));
          break;
        case CHANGED:
          iter->second->process_event(Changed{});
          assert(iter->second->is("RC"_s));
          break;
        case AGGREGATE:
          iter->second->process_event(Aggregate{});
          assert(iter->second->is("Idle"_s));
          break;
        case FIXPOINT:
          iter->second->process_event(Fixpoint{});
          assert(iter->second->is(X));
          break;
        case GOON:
          iter->second->process_event(GoOn{});
          assert(iter->second->is("Idle"_s));
          break;
        default:
          break;
      }
      return true;
    }
    return false;
  }

 private:
  std::unordered_map<GID_T, std::unique_ptr<sml::sm<GraphStateMachine>>>
      graph_state_;
  sml::sm<SystemStateMachine> system_state_;
};

}  // namespace utility
}  // namespace minigraph

#endif  // MINIGRAPH_STATE_MACHINE_H_