// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <string>

#include "lcompiler.hpp"
#include "pass.hpp"
#include "pass_lnast_tolg.hpp"
#include "pass_cprop.hpp"
#include "pass_bitwidth.hpp"

class Pass_compiler : public Pass {
protected:
  static void compile (Eprp_var &var);
public:
  explicit Pass_compiler(const Eprp_var &var);
  static std::vector<LGraph *> compile_thread (std::shared_ptr<Lnast> ln, const Eprp_var &var);  
  /* void compile_opt_thread (LGraph *lg); */
  static void  setup();
};