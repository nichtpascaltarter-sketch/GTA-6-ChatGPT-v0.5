# Combined police portable gate

Source `e048adf3f1d41394b5f9a08048cc1902b411f379` passes all 23 targets from
`./tools/test-portable.sh`, with no compiler warnings. The before/after hashes
cover 70 source, shader and test files and are identical. The completion head
`b42b1808da86039fdcde35af515dd183e61b2dff` adds only capture compatibility evidence.

The gate includes 49 original gameplay suites, 14 police integration suites,
nine resident suites, physical shot visuals, actual five-shot audio integration,
streaming, collision, visibility, timing and synthesis tests. `portable.log` is
the complete output. `driver-check.json` verifies matching native/portable target
lists and the 42 additional native scene names.

This verifies portable behavior. Native MSVC/DXC compilation, D3D12 execution
and the three actual aim/fire/reload views remain the next gate. Hardware DXR,
physical audio output and target hardware frame rate are not established.
