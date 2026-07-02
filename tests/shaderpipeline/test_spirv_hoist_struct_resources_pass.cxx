/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file test_spirv_hoist_struct_resources_pass.cxx
 * @author rdb
 * @date 2026-07-02
 */

#include "spirVHoistStructResourcesPass.h"

#include "spirv_test_utils.h"
#include "catch_amalgamated.hpp"

TEST_CASE("SpirVHoistStructResourcesPass moves resources out of structs", "[shaderpipeline]") {
  enum : uint32_t {
    id_main = 1, id_void, id_fnvoid, id_float,
    id_image, id_sampled_image, id_struct, id_ptr_uc_struct, id_var,
    id_int, id_const_0, id_const_1, id_ptr_uc_image, id_ptr_uc_float,
    id_label, id_chain_tex, id_load_tex, id_chain_f, id_load_f,
    id_bound,
  };

  // struct S { sampler2D tex; float f; }; uniform S s;  main() loads both.
  ModuleBuilder b;
  b.op(spv::OpCapability, {spv::CapabilityShader});
  b.op(spv::OpMemoryModel, {spv::AddressingModelLogical, spv::MemoryModelGLSL450});
  b.op(spv::OpEntryPoint, {spv::ExecutionModelFragment, id_main}, "main");
  b.op(spv::OpExecutionMode, {id_main, spv::ExecutionModeOriginUpperLeft});
  b.op(spv::OpName, {id_struct}, "S");
  b.op(spv::OpMemberName, {id_struct, 0}, "tex");
  b.op(spv::OpMemberName, {id_struct, 1}, "f");
  b.op(spv::OpName, {id_var}, "s");
  b.op(spv::OpTypeVoid, {id_void});
  b.op(spv::OpTypeFunction, {id_fnvoid, id_void});
  b.op(spv::OpTypeFloat, {id_float, 32});
  b.op(spv::OpTypeImage, {id_image, id_float, spv::Dim2D, 0, 0, 0, 1, spv::ImageFormatUnknown});
  b.op(spv::OpTypeSampledImage, {id_sampled_image, id_image});
  b.op(spv::OpTypeStruct, {id_struct, id_sampled_image, id_float});
  b.op(spv::OpTypePointer, {id_ptr_uc_struct, spv::StorageClassUniformConstant, id_struct});
  b.op(spv::OpTypeInt, {id_int, 32, 1});
  b.op(spv::OpConstant, {id_int, id_const_0, 0});
  b.op(spv::OpConstant, {id_int, id_const_1, 1});
  b.op(spv::OpTypePointer, {id_ptr_uc_image, spv::StorageClassUniformConstant, id_sampled_image});
  b.op(spv::OpTypePointer, {id_ptr_uc_float, spv::StorageClassUniformConstant, id_float});
  b.op(spv::OpVariable, {id_ptr_uc_struct, id_var, spv::StorageClassUniformConstant});
  b.op(spv::OpFunction, {id_void, id_main, spv::FunctionControlMaskNone, id_fnvoid});
  b.op(spv::OpLabel, {id_label});
  b.op(spv::OpAccessChain, {id_ptr_uc_image, id_chain_tex, id_var, id_const_0});
  b.op(spv::OpLoad, {id_sampled_image, id_load_tex, id_chain_tex});
  b.op(spv::OpAccessChain, {id_ptr_uc_float, id_chain_f, id_var, id_const_1});
  b.op(spv::OpLoad, {id_float, id_load_f, id_chain_f});
  b.op(spv::OpReturn, {});
  b.op(spv::OpFunctionEnd, {});

  InstructionStream stream = b.build(id_bound);
  REQUIRE(stream.validate());

  SpirVTransformer transformer(stream);
  SpirVHoistStructResourcesPass pass(true);
  transformer.run(pass);
  CHECK(transformer.validate_db());

  InstructionStream result = transformer.get_result();
  CHECK(result.validate());

  // The sampler was hoisted into its own variable.
  REQUIRE(pass._hoisted_vars.size() == 1);
  const auto &hoisted = *pass._hoisted_vars.begin();
  CHECK(hoisted.first._var_id == id_var);
  REQUIRE(hoisted.first.size() == 1);
  CHECK(hoisted.first[0] == 0);

  uint32_t hoisted_var = hoisted.second;
  CHECK(has_variable(result, hoisted_var));

  // The access chain to the sampler member was rebased onto the new variable,
  // with the struct member index dropped.
  bool found_chain = false;
  for (Instruction op : result) {
    if (op.opcode == spv::OpAccessChain && op.args[1] == id_chain_tex) {
      found_chain = true;
      CHECK(op.args[2] == hoisted_var);
      CHECK(op.nargs == 3);
    }
  }
  CHECK(found_chain);
  CHECK(find_load_pointer(result, id_sampled_image) == id_chain_tex);

  // The struct itself remains, with only the float member.
  const SpirVResultDatabase::Definition &struct_def =
    transformer.get_db().get_definition(id_struct);
  const ShaderType::Struct *struct_type = struct_def._type->as_struct();
  REQUIRE(struct_type != nullptr);
  REQUIRE(struct_type->get_num_members() == 1);
  CHECK(struct_type->get_member(0).type == ShaderType::FLOAT);
}

TEST_CASE("SpirVHoistStructResourcesPass handles an all-resource struct", "[shaderpipeline]") {
  // Regression test: a uniform struct whose members are ALL opaque resources
  // (e.g. a PBR material descriptor { sampler2D albedo; sampler2D normal; })
  // becomes empty once every member is hoisted, so the struct type -- and the
  // variable pointing at it -- are deleted.  The pass used to call resolve_type()
  // on the now-deleted struct type for the OpVariable, which asserts is_defined()
  // and crashed at draw time on any shader with such a struct (hit by
  // RenderPipeline).  It must instead hoist both samplers and drop the variable
  // without resolving the deleted type.
  enum : uint32_t {
    id_main = 1, id_void, id_fnvoid, id_float,
    id_image, id_sampled_image, id_struct, id_ptr_uc_struct, id_var,
    id_int, id_const_0, id_const_1, id_ptr_uc_image,
    id_label, id_chain_a, id_load_a, id_chain_b, id_load_b,
    id_bound,
  };

  // struct S { sampler2D a; sampler2D b; }; uniform S s;  main() loads both.
  ModuleBuilder b;
  b.op(spv::OpCapability, {spv::CapabilityShader});
  b.op(spv::OpMemoryModel, {spv::AddressingModelLogical, spv::MemoryModelGLSL450});
  b.op(spv::OpEntryPoint, {spv::ExecutionModelFragment, id_main}, "main");
  b.op(spv::OpExecutionMode, {id_main, spv::ExecutionModeOriginUpperLeft});
  b.op(spv::OpName, {id_struct}, "S");
  b.op(spv::OpMemberName, {id_struct, 0}, "a");
  b.op(spv::OpMemberName, {id_struct, 1}, "b");
  b.op(spv::OpName, {id_var}, "s");
  b.op(spv::OpTypeVoid, {id_void});
  b.op(spv::OpTypeFunction, {id_fnvoid, id_void});
  b.op(spv::OpTypeFloat, {id_float, 32});
  b.op(spv::OpTypeImage, {id_image, id_float, spv::Dim2D, 0, 0, 0, 1, spv::ImageFormatUnknown});
  b.op(spv::OpTypeSampledImage, {id_sampled_image, id_image});
  b.op(spv::OpTypeStruct, {id_struct, id_sampled_image, id_sampled_image});
  b.op(spv::OpTypePointer, {id_ptr_uc_struct, spv::StorageClassUniformConstant, id_struct});
  b.op(spv::OpTypeInt, {id_int, 32, 1});
  b.op(spv::OpConstant, {id_int, id_const_0, 0});
  b.op(spv::OpConstant, {id_int, id_const_1, 1});
  b.op(spv::OpTypePointer, {id_ptr_uc_image, spv::StorageClassUniformConstant, id_sampled_image});
  b.op(spv::OpVariable, {id_ptr_uc_struct, id_var, spv::StorageClassUniformConstant});
  b.op(spv::OpFunction, {id_void, id_main, spv::FunctionControlMaskNone, id_fnvoid});
  b.op(spv::OpLabel, {id_label});
  b.op(spv::OpAccessChain, {id_ptr_uc_image, id_chain_a, id_var, id_const_0});
  b.op(spv::OpLoad, {id_sampled_image, id_load_a, id_chain_a});
  b.op(spv::OpAccessChain, {id_ptr_uc_image, id_chain_b, id_var, id_const_1});
  b.op(spv::OpLoad, {id_sampled_image, id_load_b, id_chain_b});
  b.op(spv::OpReturn, {});
  b.op(spv::OpFunctionEnd, {});

  InstructionStream stream = b.build(id_bound);
  REQUIRE(stream.validate());

  SpirVTransformer transformer(stream);
  SpirVHoistStructResourcesPass pass(true);
  // This must not assert (the regression).
  transformer.run(pass);
  CHECK(transformer.validate_db());

  InstructionStream result = transformer.get_result();
  CHECK(result.validate());

  // Both samplers were hoisted into their own variables.
  REQUIRE(pass._hoisted_vars.size() == 2);
  for (const auto &hoisted : pass._hoisted_vars) {
    CHECK(hoisted.first._var_id == id_var);
    CHECK(has_variable(result, hoisted.second));
  }

  // The now-empty struct and its variable were dropped entirely.
  CHECK_FALSE(has_variable(result, id_var));
  CHECK(count_op(result, spv::OpTypeStruct) == 0);

  // Both access chains were rebased onto their hoisted variables (member index
  // dropped), so the loads still resolve.
  int rebased = 0;
  for (Instruction op : result) {
    if (op.opcode == spv::OpAccessChain &&
        (op.args[1] == id_chain_a || op.args[1] == id_chain_b)) {
      ++rebased;
      CHECK(op.nargs == 3);
      CHECK(has_variable(result, op.args[2]));
    }
  }
  CHECK(rebased == 2);
}
