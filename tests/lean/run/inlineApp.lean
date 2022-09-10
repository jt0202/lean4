import Lean

def f (x : Nat) :=
  (x - 1) + x * 2 + x*x

def h (x : Nat) :=
  inline <| f (x + x)

#eval Lean.Compiler.compile #[``h]

open Lean Compiler LCNF in
@[cpass] def simpInline : PassInstaller :=
  Testing.assertDoesNotContainConstAfter `simp `simpInlinesInline ``inline "simp did not inline `inline`"

set_option trace.Compiler.result true
#eval Lean.Compiler.compile #[``h]
