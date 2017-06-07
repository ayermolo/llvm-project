; RUN: llc < %s | FileCheck %s

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; CHECK: .section .odrtab,"e",@llvm_odrtab
; CHECK: .ascii "foo"

!0 = !{!"foo"}
!llvm.odrtab = !{!0}
