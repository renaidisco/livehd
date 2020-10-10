//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>

#include "cprop.hpp"
#include "lbench.hpp"
#include "lgcpp_plugin.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgtuple.hpp"
#include "pass_cprop.hpp"

#define TRACE(x)
//#define TRACE(x) x



Cprop::Cprop (bool _hier) : hier(_hier) {}

void Cprop::collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type_op();

  bool all_done = true;
  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type_op() != op) {
      all_done = false;
      continue;
    }
    if (out.driver.get_pid() != out.sink.get_pid()) { //FIXME: maybe separate different op 
      all_done = false;
      continue;
    }

    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop same_op pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      if (op == Ntype_op::Xor) {
        // fmt::print("cprop xor pin:{} node.size:{} sink.size:{}\n",inp.driver.debug_name(), node.get_num_edges(),
        // inp.driver.get_node().get_num_edges()); if (out.sink.is_connected(inp.driver)) {
        if (inp.driver.is_connected(out.sink)) {
          out.sink.del_driver(inp.driver);
        } else {
          out.sink.connect_driver(inp.driver);
        }
      } else if (op == Ntype_op::Or || op == Ntype_op::And) {
        // fmt::print("cprop simplified forward or/and pin:{}\n",inp.driver.debug_name());
        out.sink.connect_driver(inp.driver);
      } else {
        I(op != Ntype_op::Sum);  // handled at collapse_forward_sum
        out.sink.connect_driver(inp.driver);
      }
    }

    TRACE(fmt::print("cprop same_op del_edge pin:{} to pin:{}\n", out.driver.debug_name(), out.sink.debug_name()));
    out.del_edge();
  }
  if (all_done) {
    I(!node.has_outputs());
    node.del_node();
  }
}

void Cprop::collapse_forward_sum(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type_op();
  I(op == Ntype_op::Sum);
  bool all_edges_deleted = true;
  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type_op() != Ntype_op::Sum) {
      all_edges_deleted = false;
      continue;
    }

    auto next_sum_node = out.sink.get_node();
    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop same_op pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      auto sink_name = Ntype::get_sink_name(Ntype_op::Sum, inp.sink.get_pid()); //use get_pin_name or pin_raw
      auto next_sum_spin = next_sum_node.setup_sink_pin(sink_name);  // Connect same PID
      next_sum_spin.connect_driver(inp.driver);
    }
    TRACE(fmt::print("cprop same_op del_edge pin:{} to pin:{}\n", out.driver.debug_name(), out.sink.debug_name()));
    out.del_edge();
  }

  if (all_edges_deleted) {
    node.del_node();
  }
}

#if 0
void Cprop::collapse_forward_shiftleft(Node &node) {
  I(node.get_type_op()==ShiftLeft_Op);

  // a <<n | b -> join(a,b) if b.bits>=n
  for (auto &out : node.out_edges()) {
    auto sink_node = out.sink.get_node();
    if (sink_node.get_type_op() != Or_Op) continue;

    if (!sink_node.get_driver_pin(0).has_outputs()) continue;

    HERE
  }
}
#endif

// Collase forward single node but only for pid!=0 (not reduction ops)
void Cprop::collapse_forward_always_pin0(Node &node, XEdge_iterator &inp_edges_ordered) {
  bool can_delete = true;

  auto op = node.get_type_op();

  for (auto &out : node.out_edges()) {
    /* if (out.driver.get_pid()) { */
    /*   can_delete = false; */
    /*   continue; */
    /* } */

    for (auto &inp : inp_edges_ordered) {
      /* if (inp.sink.get_pid()) { */
      /*   can_delete = false; */
      /*   continue; */
      /* } */
      TRACE(fmt::print("cprop forward_always pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      if (op == Ntype_op::Xor) {
        if (inp.driver.is_connected(out.sink)) {
          out.sink.del_driver(inp.driver);
        } else {
          out.sink.connect_driver(inp.driver);
        }
      } else {
        out.sink.connect_driver(inp.driver);
      }
    }
  }

  if (can_delete) {
    TRACE(fmt::print("cprop forward_always del_node node:{}\n", node.debug_name()));
    node.del_node();
  }
}

void Cprop::collapse_forward_for_pin(Node &node, Node_pin &new_dpin) {
  for (auto &out : node.out_edges()) {
    new_dpin.connect_sink(out.sink);
  }

  node.del_node();
}

void Cprop::try_constant_prop(Node &node, XEdge_iterator &inp_edges_ordered) {
  int n_inputs_constant = 0;
  int n_inputs          = 0;
  for (auto e : inp_edges_ordered) {
    n_inputs++;
    if (e.driver.get_node().is_type_const())
      n_inputs_constant++;
  }

  if (n_inputs == n_inputs_constant && n_inputs) {
    replace_all_inputs_const(node, inp_edges_ordered);
  } else if (n_inputs) {
    replace_part_inputs_const(node, inp_edges_ordered);
  }
}

void Cprop::try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered) {
  // No need to collapse things like const -> join because the Lconst will be forward eval

  auto op = node.get_type_op();

  if (inp_edges_ordered.size() == 1) {
    if (op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::Div || op == Ntype_op::And || op == Ntype_op::Or
        || op == Ntype_op::Xor) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
  }

  if (op == Ntype_op::Sum) {
    collapse_forward_sum(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mult || op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mux) {
    // If all the options are the same. Collapse forward
    auto &a_pin = inp_edges_ordered[1].driver;
    for (auto i = 2u; i < inp_edges_ordered.size(); ++i) {
      if (a_pin != inp_edges_ordered[i].driver)
        return;
    }
    collapse_forward_for_pin(node, a_pin);
  }
}

void Cprop::replace_part_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type_op();
  if (op == Ntype_op::Mux) {
    auto s_node = inp_edges_ordered[0].driver.get_node();
    if (!s_node.is_type_const())
      return;

    I(s_node.get_type_const().is_i());  // string with ??? in mux? Give me test case to debug
    size_t sel = s_node.get_type_const().to_i();

    Node_pin a_pin;
    if ((sel + 1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
      a_pin = node.get_class_lgraph()->create_node_const(0).get_driver_pin();
    } else {
      a_pin = inp_edges_ordered[sel + 1].driver;
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::Sum) {
    int n_replace = 0;
    for (auto &i : inp_edges_ordered) {
      if (i.driver.get_node().is_type_const())
        n_replace++;
    }

    if (n_replace > 1) {
      Lconst result;
      for (auto &i : inp_edges_ordered) {
        if (!i.driver.get_node().is_type_const())
          continue;

        auto c = i.driver.get_node().get_type_const();
        if (i.sink.get_pid() == 0) { //FIXME: change to A
          result = result + c;
        } else if (i.sink.get_pid() == 1){
          result = result - c;
        }
        i.del_edge();
      }
      auto new_node = node.get_class_lgraph()->create_node_const(result);
      auto dpin     = new_node.get_driver_pin();
      if (result < 0) {
        node.setup_sink_pin("B").connect_driver(dpin);  // signed pin
      } else {
        node.setup_sink_pin("A").connect_driver(dpin);  // unsigned pin
      }
    }
  }
}

void Cprop::replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {
  // simple constant propagation
  auto op = node.get_type_op();
  if (op == Ntype_op::SHL) {
    Lconst val = node.get_sink_pin("A").get_driver_node().get_type_const();
    Lconst amt = node.get_sink_pin("B").get_driver_node().get_type_const();

    Lconst result = val << amt;

    TRACE(fmt::print("cprop: shl to {} ({}<<{})\n", result.to_pyrope(), val.to_pyrope(), amt.to_pyrope()));

    replace_node(node, result);
  } else if (op == Ntype_op::Sum) {
    Lconst result;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (i.sink.get_pid() == 0) {
        result = result + c;
      } else { //pid = 1
        result = result - c;
      }
    }

    TRACE(fmt::print("cprop: add node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Ntype_op::Or) {
    Bits_t max_bits = 0;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (c.get_bits() > max_bits)
        max_bits = c.get_bits();
    }
    Lconst result(0);
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      result = result.or_op(c.adjust_bits(max_bits));
    }

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    Lconst result_reduced = result == 0 ? 0 : 1;

    replace_logic_node(node, result, result_reduced);

  } else if (op == Ntype_op::And) {
    Bits_t max_bits = 0;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (c.get_bits() > max_bits)
        max_bits = c.get_bits();
    }
    Lconst result("-1s");
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      result = result.and_op(c.adjust_bits(max_bits));
    }

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    Lconst result_reduced = result == Lconst("-1s") ? 1 : 0;

    replace_logic_node(node, result, result_reduced);

  } else if (op == Ntype_op::EQ) {
    bool eq = true;
    I(inp_edges_ordered.size() > 1);
    auto first = inp_edges_ordered[0].driver.get_node().get_type_const();
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      auto c = inp_edges_ordered[i].driver.get_node().get_type_const();
      eq     = eq && first.eq_op(c);
    }

    Lconst result(eq ? 1 : 0);

    TRACE(fmt::print("cprop: eq node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Ntype_op::Mux) {
    auto sel_const = inp_edges_ordered[0].driver.get_node().get_type_const();
    I(sel_const.is_i());  // string with ??? in mux? Give me test case to debug

    size_t sel = sel_const.to_i();

    Lconst result;
    if ((sel + 1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
    } else {
      result = inp_edges_ordered[sel + 1].driver.get_node().get_type_const();
    }

    replace_node(node, result);
  } else {
    fmt::print("FIXME: cprop still does not copy prop node:{}\n", node.debug_name());
  }
}

void Cprop::replace_node(Node &node, const Lconst &result) {
  auto new_node = node.get_class_lgraph()->create_node_const(result);
  auto dpin     = new_node.get_driver_pin();

  for (auto &out : node.out_edges()) {
    if (dpin.get_bits() == out.driver.get_bits() || out.driver.get_bits() == 0) {
      TRACE(fmt::print("cprop: const:{} to out.driver:{}\n", result.to_pyrope(), out.driver.debug_name()));
      dpin.connect_sink(out.sink);
    } else {
      // create new const node to preserve bits
      auto result2 = result.adjust_bits(out.driver.get_bits());

      auto dpin2 = node.get_class_lgraph()->create_node_const(result2).get_driver_pin();

      TRACE(fmt::print("creating const:{} {}bits {}  from const:{} {}bits\n",
                       result2.to_pyrope(),
                       out.driver.get_bits(),
                       dpin2.get_bits(),
                       result.to_pyrope(),
                       dpin.get_bits()));

      dpin2.connect_sink(out.sink);
    }

    // out.del_edge();
  }

  node.del_node();
}

// FIXME: not sure
void Cprop::replace_logic_node(Node &node, const Lconst &result, const Lconst &result_reduced) {
  Node_pin dpin_0;
  Node_pin dpin_1;

  for (auto &out : node.out_edges()) {
    if (out.driver.get_pid()) {
      // Reduction
      if (dpin_1.is_invalid()) {
        dpin_1 = node.get_class_lgraph()->create_node_const(result_reduced).get_driver_pin();
      }
      dpin_1.connect_sink(out.sink);
    } else {
      // bitwise op
      if (dpin_0.is_invalid()) {
        dpin_0 = node.get_class_lgraph()->create_node_const(result).get_driver_pin();
      }
      dpin_0.connect_sink(out.sink);
    }
  }

  node.del_node();
}

void Cprop::process_subgraph(Node &node) {
  if (node.is_type_sub_present())
    return;

  auto *sub = node.ref_type_sub_node();

  const auto &reg = Lgcpp_plugin::get_registry();

  auto it = reg.find(sub->get_name());
  if (it == reg.end())
    return;

  fmt::print("cprop subgraph:{} is not present, found lgcpp...\n", sub->get_name());

  std::shared_ptr<Lgtuple> inp;
  std::shared_ptr<Lgtuple> out;
  it->second(node.get_class_lgraph(), inp, out);

  if (!out) { // no out tuple populated
    return;
  }
  fmt::print("cprop subgraph:{} has out\n", sub->get_name());
  out->dump("  ");

  for (auto dpin : node.out_connected_pins()) {
    fmt::print("dpin:{} pid:{} testing...\n", dpin.debug_name(), dpin.get_pid());
    if (dpin.has_name()) {
      if (out->has_key_name(dpin.get_name())) {
        fmt::print("replace dpin:{}\n", dpin.get_name());
      } else {
        fmt::print("dpin:{} disconnected. name Remove\n", dpin.get_name());
      }
    } else {
      if (out->has_key_pos(dpin.get_pid())) {
        fmt::print("replace dpin:{} pid:{}\n", dpin.debug_name(), dpin.get_pid());
      } else {
        fmt::print("dpin:{} disconnected. pos Remove\n", dpin.debug_name());
      }
    }
  }

#if 1
  Port_ID instance_pid = 0;
  for (const auto &io_pin : sub->get_io_pins()) {
    instance_pid++;
    if (io_pin.is_input())
      continue;
    if (out->has_key_name(io_pin.name)) {
      fmt::print("replace io_pin:{}\n", io_pin.name);
    } else {
      fmt::print("disconnected io_pin:{}\n", io_pin.name);
    }

    fmt::print("iopin:{} pos:{} instance_pid:{}...\n", io_pin.name, io_pin.graph_io_pos, instance_pid);
  }
#endif
}

void Cprop::process_attr_q_pin(Node &node, Node_pin &parent_dpin) {
  // Get variable name
  auto driver_wname = parent_dpin.get_name();

  // remove the SSA from name
  auto pos   = driver_wname.find_last_of('_');
  auto wname = driver_wname.substr(0, pos);

  if (wname == driver_wname) {
    collapse_forward_for_pin(node, parent_dpin);
  } else {
    // Find flop
    auto target_ff_qpin = Node_pin::find_driver_pin(node.get_class_lgraph(), wname);
    collapse_forward_for_pin(node, target_ff_qpin);
  }
}

bool Cprop::process_attr_get(Node &node) {
  I(node.get_type_op() == Ntype_op::AttrGet);

  if (!node.is_sink_connected("name") || !node.is_sink_connected("field"))
    return false;

  // Either pos or name
  auto parent_dpin   = node.get_sink_pin("name").get_driver_pin();
  auto key_name_dpin = node.get_sink_pin("field").get_driver_pin();

  I(key_name_dpin.get_node().get_type_op() == Ntype_op::TupKey);
  I(key_name_dpin.has_name());
  auto key_name = key_name_dpin.get_name();
  if (key_name.substr(0, 2) == "__") {
    if (key_name.substr(0, 7) == "__q_pin") {
      fmt::print("process_attr_q_pin parent_dpin:{} node:{}\n", parent_dpin.debug_name(), node.debug_name());
      process_attr_q_pin(node, parent_dpin);
      return true;
    }
  }
  return false;
}

std::tuple<std::string_view, std::string_view, int> Cprop::get_tuple_name_key(Node &node) {
  std::string_view tup_name;
  std::string_view key_name;
  int              key_pos = -1;
  if (node.is_sink_connected("field")) {
    auto node2 = node.get_sink_pin("field").get_driver_node();
    if (node2.get_type_op() == Ntype_op::TupKey)
      key_name = node2.get_driver_pin().get_name();
  }

  for(auto dpin : node.get_sink_pin("tuple_name").inp_driver()) {
    if (dpin.has_name()) {
      tup_name = dpin.get_name();
      break;
    }
  }

  if (node.is_sink_connected("position")) {
    auto node2 = node.get_sink_pin("position").get_driver_node();
    if (node2.is_type_const())
      key_pos = node2.get_type_const().to_i();
  }

  // I(!key_name.empty() || key_pos != -1);  // At least one defined // FIXME->sh: not necessarily true, could be resolved at later
  // TupAdd merge step

  return std::make_tuple(tup_name, key_name, key_pos);
}

bool Cprop::process_tuple_get(Node &node) {
  I(node.get_type_op() == Ntype_op::TupGet);

  auto parent_dpin = node.get_sink_pin("tuple_name").get_driver_pin();
  auto parent_node = parent_dpin.get_node();

  auto [tup_name, key_name, key_pos] = get_tuple_name_key(node);

  // special case when TG try to get a scalar variable by accessing pos 0
  // FIXME:sh-> should be handled by tup.is_scalar()
  if (parent_node.get_type_op() != Ntype_op::TupAdd && key_pos == 0 && !parent_dpin.is_invalid()) {
    collapse_forward_for_pin(node, parent_dpin);
    return true;
  }


  auto ptup_it = node2tuple.find(parent_node.get_compact());
  if (ptup_it == node2tuple.end()) {  // ptup_it = parent_node
    //FIXME:sh-> if the parent comes from unified input $, we should just give it a empty lgtuple

    std::string key(key_name);
    if (key.empty())
      key = std::to_string(key_pos);

    
    Pass::error("for tuple_get {} parent_node {}, there is no tuple of {}, so no valid field:{}\n",
                node.debug_name(),
                parent_node.debug_name(),
                tup_name,
                key);
    return false;
  }

  auto ctup = ptup_it->second;
  //FIXME:sh -> should exclude the TG of $
  if (!ctup->has_key(key_pos, key_name)) {
    ctup->dump();
    Pass::error("tuple {} does not have field pos:{} key:{}\n", tup_name, key_pos, key_name);
    return false;
  }

  std::shared_ptr<Lgtuple> sub_tup;
  if (key_pos == 0 && ctup->is_scalar())
    sub_tup = ctup;
  else
    sub_tup = ctup->get_tuple(key_pos, key_name);

  if (!sub_tup) {
    return false; // Could not resolve (maybe compile error, maybe hierarchical needed)
  }


  // still unclear if the TupGet chain is resolved (final TupGet will decide)
  if (!sub_tup->is_valid_val_dpin()) { 
    node2tuple[node.get_compact()] = sub_tup;
    return true;   
  }

  auto val_dpin = sub_tup->get_value_dpin();
  I(!val_dpin.is_invalid());

  if (sub_tup->is_scalar()) {
    collapse_forward_for_pin(node, val_dpin);
    return true;
  }

  bool all_tuples = true;
  bool any_tuples = false;
  for(auto e:node.out_edges()) {
    auto t = e.sink.get_node().is_type_tup();
    all_tuples = all_tuples && t;
    any_tuples = any_tuples || t;
  }

  if (all_tuples) {
    node2tuple[node.get_compact()] = sub_tup;
  } else {
    I(!any_tuples); // If so, must create attrset for non tup and keep up (conta==0 opt out)
    int conta=0;
    for(auto it : sub_tup->get_all_attributes()) {
      auto attr_key_node = node.get_lg()->create_node(Ntype_op::TupKey);
      auto attr_key_dpin = attr_key_node.setup_driver_pin();
      attr_key_dpin.set_name(it.first);

      if (conta==0) {
        fmt::print("cprop: changing node:{} to AttrSet node for attr:{} from pin:{}\n",node.debug_name(), it.first, it.second.debug_name());
        // Reuse current node. First delete input edges
        for(auto e:node.inp_edges()) {
          e.del_edge();
        }
        node.set_type(Ntype_op::AttrSet);
        node.setup_sink_pin("var_name").connect_driver(val_dpin);
        node.setup_sink_pin("field").connect_driver(attr_key_dpin);
        node.setup_sink_pin("value").connect_driver(it.second);
      } else {
        I(false); // handle multiple attr set (create node)
      }
      conta++;
    }
  }
  return true;
}

std::shared_ptr<Lgtuple> Cprop::process_tuple_add_chain(Node_pin up_dpin) {

  auto up_node = up_dpin.get_node();
  auto ptup_it = node2tuple.find(up_node.get_compact());
  if (ptup_it == node2tuple.end()) {
    return nullptr;
  }

  I(up_node.get_type_op() == Ntype_op::TupAdd || up_node.get_type_op() == Ntype_op::TupGet || up_node.get_type_op() == Ntype_op::TupRef);

  return std::make_shared<Lgtuple>(*(ptup_it->second));
}

void Cprop::process_tuple_add(Node &node) {
  // Can not delete TupAdd here. Only TupGet can delete up chain if nobody needs the TupAdd
  I(node.get_type_op() == Ntype_op::TupAdd);

  // ptup == parent_tup == up_tup
  std::shared_ptr<Lgtuple> ptup; 
  if (node.get_sink_pin("tuple_name").is_connected())
    ptup = process_tuple_add_chain(node.get_sink_pin("tuple_name").get_driver_pin());

  // a new tuple chain as the val_dpin, either for a tuple concat or a new tuple hierarchy
  std::shared_ptr<Lgtuple> chain_tup; 
  if(node.is_sink_connected("value"))
    chain_tup = process_tuple_add_chain(node.get_sink_pin("value").get_driver_pin());

  auto [tup_name, key_name, key_pos] = get_tuple_name_key(node);

  std::shared_ptr<Lgtuple> ctup;
  if (chain_tup) { 
    if (ptup) {
      //fmt::print("1.TupAdd node:{} tup_name:{} pos:{} key:{}\n", node.debug_name(), tup_name, key_pos, key_name);
      ctup = std::make_shared<Lgtuple>(*ptup);
    } else {
      //fmt::print("2.TupAdd node:{} tup_name:{} pos:{} key:{}\n", node.debug_name(), tup_name, key_pos, key_name);
      ctup = std::make_shared<Lgtuple>(tup_name);
    }

    if (key_pos<0 && key_name.empty()) { // dummy TA -> Tuple Concatenation operator
      bool ok = ctup->add(chain_tup);
      if (!ok) {
        ptup->dump();
        chain_tup->dump();
        Pass::error("tuples name:{} pos:{} key:{} can not be merged\n", tup_name, key_pos, key_name);
        return;
      }
    } else {
      ctup->set(key_name, chain_tup);  // add hier-tuple field. FIXME: create lgtuple:set(pos,key,tuple)
    }
  } else {
    if (ptup) {
      //fmt::print("3.TupAdd node:{} tup_name:{} pos:{} key:{}\n", node.debug_name(), tup_name, key_pos, key_name);
      ctup = ptup;
    } else {
      //fmt::print("4.TupAdd node:{} tup_name:{} pos:{} key:{}\n", node.debug_name(), tup_name, key_pos, key_name);
      ctup = std::make_shared<Lgtuple>(tup_name);
    }
    if (node.is_sink_connected("value")) {
      auto val_dpin = node.get_sink_pin("value").get_driver_pin();
      if (key_pos<0 && key_name.empty()) { // Tuple Concatenation operator
        if (!ptup && node.get_sink_pin("tuple_name").is_connected()) {
          ctup->add(node.get_sink_pin("tuple_name").get_driver_pin());
        }
        ctup->add(val_dpin);
      } else {
        bool ok = ctup->set(key_pos, key_name, val_dpin);
        if (!ok) {
          Pass::error("new tuple {} could not add field pos:{} name:{}\n", tup_name, key_pos, key_name);
          return;
        }
      }
    } else {
      I(ptup); // tup1 = tup2 can have no sink(3)
    }
  }
#if 0
  if (ptup) {
    fmt::print("Parent:{}\n", node.get_sink_pin(0).get_driver_node().debug_name());
    ptup->dump();
  }
  if (chain_tup) {
    fmt::print("Chain:{}\n", node.get_sink_pin(3).get_driver_node().debug_name());
    chain_tup->dump();
  }
  fmt::print("current:{}\n", node.debug_name());
  ctup->dump();
#endif

  node2tuple[node.get_compact()] = ctup;
}

void Cprop::do_trans(LGraph *lg) {
  /* Lbench b("pass.cprop"); */

  bool tup_get_left = false;

  for (auto node : lg->forward()) {
    auto op = node.get_type_op();
    // fmt::print("NEXT: node:{}\n",node.debug_name());

    // Special cases to handle in cprop
    if (op == Ntype_op::AttrGet) {
      process_attr_get(node);
      continue;
    } else if (op == Ntype_op::AttrSet) {
      continue;  // Nothing to do in cprop
    } else if (op == Ntype_op::Sub) {
      process_subgraph(node);
      continue;
    } else if (op == Ntype_op::Sflop || op == Ntype_op::Aflop || op == Ntype_op::Latch || op == Ntype_op::Fflop || op == Ntype_op::Memory || op == Ntype_op::Sub) {
      fmt::print("cprop skipping node:{}\n", node.debug_name());
      // FIXME: if flop feeds itself (no update, delete, replace for zero)
      // FIXME: if flop is disconnected *after AttrGet processed*, the flop was not used. Delete
      continue;
    } else if (!node.has_outputs()) {
      node.del_node();
      continue;
    } else if (op == Ntype_op::TupAdd) {
      process_tuple_add(node);
      continue;
    } else if (op == Ntype_op::TupGet) {
      auto ok = process_tuple_get(node);
      if (!ok) {
        fmt::print("cprop could not simplify node:{}\n",node.debug_name());
      }
      tup_get_left |= !ok;
      continue;
    }

    // Normal copy prop and strength reduction
    auto inp_edges_ordered = node.inp_edges_ordered();
    try_constant_prop(node, inp_edges_ordered);

    if (node.is_invalid())
      continue;  // It got deleted

#if 0
    if (inp_edges_ordered.size() > 64) {
#ifndef NDEBUG
      fmt::print("node:{} is already quite large. Skipping cprop\n", node.debug_name());
#endif
      continue;
    }
    // fmt::print("node:{} inp:{} out:{}\n",node.debug_name(), node.get_num_inputs(), node.get_num_out_edges());
#endif

    try_collapse_forward(node, inp_edges_ordered);
  }

  for (auto node : lg->fast()) {
    if (!tup_get_left && node.is_type_tup()) {
      if (hier) {
        auto it = node2tuple.find(node.get_compact());
        if (it != node2tuple.end()) {
          node2tuple.erase(it);
        }
      }
      node.del_node();
      continue;
    }

    if (!node.has_outputs()) {
      auto op = node.get_type_op();
      if (op != Ntype_op::Sflop && op != Ntype_op::Aflop && op != Ntype_op::Latch && op != Ntype_op::Fflop && op != Ntype_op::Memory && op != Ntype_op::Sub) {
        // TODO: del_dead_end_nodes(); It can propagate back and keep deleting
        // nodes until it reaches a SubGraph or a driver_pin that has some
        // other outputs. Doing this dead_end_nodes delete iterator can retuce
        // the number of times that cprop needs to be called for deep chains.
        node.del_node();
      }
      continue;
    }
  }

  if (!hier) {
    node2tuple.clear();
  }
}

void Cprop::dump_node2tuples() const {

  for(const auto it:node2tuple) {
    fmt::print("node nid:{}\n",it.first.get_nid());
    it.second->dump();
  }
}
