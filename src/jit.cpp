#include "jit.h"
#include "runtime.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/TargetSelect.h>

#include <memory>

namespace fluxion {

extern "C" void llvm_orc_registerEHFrameSectionWrapper();
extern "C" void llvm_orc_deregisterEHFrameSectionWrapper();

int run_jit(GeneratedModule generated) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto jit_or_error = llvm::orc::LLJITBuilder().create();
  if (!jit_or_error) {
    throw DiagnosticError({"<jit>", 1, 1}, llvm::toString(jit_or_error.takeError()));
  }
  auto jit = std::move(*jit_or_error);
  llvm::orc::MangleAndInterner mangle(jit->getExecutionSession(), jit->getDataLayout());

  llvm::orc::SymbolMap symbols;
  symbols[mangle("fluxion_print_f64")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_print_f64), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_print_i32")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_print_i32), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_print_bool")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_print_bool), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_print_string")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_print_string), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_print_matrix")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_print_matrix), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_pow_i32")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_pow_i32), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_pow_f64")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_pow_f64), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_sin")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_sin), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_cos")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_cos), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_tan")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_tan), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_asin")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_asin), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_acos")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_acos), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_atan")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_atan), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_atan2")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_atan2), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_sqrt")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_sqrt), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_exp")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_exp), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_log")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_log), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_math_log10")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_math_log10), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_matrix_create")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_matrix_create), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_matrix_set")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_matrix_set), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_matrix_add")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_matrix_add), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_matrix_sub")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_matrix_sub), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_matrix_mul")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_matrix_mul), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_viz_cartpole")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_viz_cartpole), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_viz_cartpole_shove")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_viz_cartpole_shove), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_otel_span_start")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_otel_span_start), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_otel_span_end")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_otel_span_end), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_otel_event_i32")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_otel_event_i32), llvm::JITSymbolFlags::Exported);
  symbols[mangle("fluxion_otel_event_f64")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&fluxion_otel_event_f64), llvm::JITSymbolFlags::Exported);
  symbols[mangle("llvm_orc_registerEHFrameSectionWrapper")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&llvm_orc_registerEHFrameSectionWrapper), llvm::JITSymbolFlags::Exported);
  symbols[mangle("llvm_orc_deregisterEHFrameSectionWrapper")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&llvm_orc_deregisterEHFrameSectionWrapper), llvm::JITSymbolFlags::Exported);
  if (auto err = jit->getMainJITDylib().define(llvm::orc::absoluteSymbols(symbols))) {
    throw DiagnosticError({"<jit>", 1, 1}, llvm::toString(std::move(err)));
  }

  auto thread_safe_module =
      llvm::orc::ThreadSafeModule(std::move(generated.module), std::move(generated.context));
  if (auto err = jit->addIRModule(std::move(thread_safe_module))) {
    throw DiagnosticError({"<jit>", 1, 1}, llvm::toString(std::move(err)));
  }

  auto main_symbol = jit->lookup("main");
  if (!main_symbol) {
    throw DiagnosticError({"<jit>", 1, 1}, llvm::toString(main_symbol.takeError()));
  }
  using MainFn = int (*)();
#if LLVM_VERSION_MAJOR >= 17
  auto* main_fn = main_symbol->toPtr<MainFn>();
#else
  auto* main_fn = main_symbol->getAddress().toPtr<MainFn>();
#endif
  return main_fn();
}

}  // namespace fluxion
