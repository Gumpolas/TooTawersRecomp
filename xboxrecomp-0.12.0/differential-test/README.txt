Differential instruction test: lifts each instruction shape found in the XBE
with tools/recomp and compares it against the same instruction executed
natively (x86-64 host, 32-bit addressing into a low mapping). Linux + clang.
  python3 shapes.py                      # enumerate shapes -> shapes.pkl
  python3 gen.py add,sub,...             # or gen_x87.py for x87 sequences
  clang -c -I. cases.S; clang -O0 -c -I<gen dir> -I. -w cases.c
  clang -O1 -DRECOMP_GENERATED_CODE -c -I<gen dir> -I. -w main.c
  clang -pie main.o cases.o cases_s.o -o harness -lm; ./harness 300
BAD = mismatch, GAP = the lifter has no model for that flag consumer.
