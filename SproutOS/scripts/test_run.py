#!/usr/bin/env python3
"""测试猜数字程序运行，传入预设输入"""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from seedvm_test import VM

# 读取编译好的 seed
vm = VM("test_guess.seed")
print(f"=== {vm.name} ===")

# 预设输入序列：先猜3，再猜7（假设目标是7），再猜5
preset_inputs = ["3", "7", "5", "2", "8"]
input_idx = 0

total_steps = 0
while not vm.halted and total_steps < 50000:
    steps = vm.run(max_steps=3000)
    total_steps += steps
    print(f"--- {total_steps} steps, halted={vm.halted}, waiting={vm.waiting_input} ---")
    print("OUTPUT:", repr("".join(vm.out)))
    
    if vm.waiting_input:
        if input_idx < len(preset_inputs):
            inp = preset_inputs[input_idx]
            print(f"<<< AUTO INPUT: {inp}")
            vm.input_queue.append(inp)
            input_idx += 1
        else:
            print("!!! NO MORE PRESET INPUTS")
            break
    else:
        break

print("\n=== FINAL OUTPUT ===")
print("".join(vm.out))
print(f"=== total steps: {total_steps} ===")
print(f"=== locals: {vm.locals} ===")
