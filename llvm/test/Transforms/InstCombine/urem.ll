; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt < %s -instcombine -S | FileCheck %s

define i64 @rem_unsigned(i64 %x1, i64 %y2) {
; CHECK-LABEL: @rem_unsigned(
; CHECK-NEXT:    [[R:%.*]] = urem i64 %x1, %y2
; CHECK-NEXT:    ret i64 [[R]]
;
  %r = udiv i64 %x1, %y2
  %r7 = mul i64 %r, %y2
  %r8 = sub i64 %x1, %r7
  ret i64 %r8
}