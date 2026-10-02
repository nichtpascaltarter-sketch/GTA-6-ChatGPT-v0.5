# Police pose validation

The authored officer weapon now follows `Game::policePose`: observation-based aim,
an actual emitted shot's short recoil/flash/tracer, and a magazine reload. At full
aim, the barrel tip coincides with the physical shot origin. The tracer ends at
the physically resolved impact. Static held aim and wanted level alone do not
create a muzzle event. The existing bounded light selection receives a short
point light from the same emitted-shot envelope.

`tests/police_visuals_tests.cpp` advances real game state and checks the complete
aim/fire/reload cycle, both near and far character detail, finite geometry and
unit normals, magazine movement/removal, and barrel continuity at reload exit.
It checks every simulation step behind cover, plus pause and death suppression.
No synthetic pose or shot is injected into the game state.

`non_police_probe.cpp` compares all vertex attributes and indices for 16 resident
activity/detail combinations, the player, garage attendant, trial marshal, and
four passenger vehicle types. The baseline substitutes only `src/visuals.cpp`
from commit `70126b84943c431095d556b9b7b2730ad64b9cbc`; all other game objects are
the same. All 23 meshes match exactly. These are deterministic geometry checks,
not a substitute for native image inspection.

The focused reload regression was also run against the earlier implementation
that kept `raise=1` throughout reload. It failed with
`reload completion popped the gun before its aim ramp`. The repaired final 20%
of reload lowers the weapon into the following aim ramp. The failing earlier
visual source had SHA-256
`612cf7de14bb33ff1a3011d81f7a4a38a66e1eda707c81809446181cd243f4c8`.

Reproduce the strict, ASan/UBSan/leak and fingerprint checks from the repository:

```sh
bash validation/police/visuals/run.sh
```

The script requires Bash, Git and a C++20 compiler, writes all objects/executables
under ignored `build/police-visual-evidence`, and verifies that checked source
hashes remain unchanged during the run. No external code or assets are used.
The archived logs identify the final checked source. Windows compilation,
D3D12 debug validation and native police screenshots remain separate gates.

Archived result: both pose suites pass strict C++20 and ASan/UBSan with leak
detection. The police sequence inspects 49 aim/reload samples plus emitted-shot
and suppression checks; the resident suite covers all eight authored bench
anchors at both character detail levels. The 23-case mesh comparison has an
empty diff. Checks used GCC 14.2 on Linux. To avoid duplicating the concurrent
gameplay gate's compilation, its source-verified common objects were reused;
the focused test executables and fingerprint probes were linked and run
separately. The repaired save-loader `game.cpp` object was included in both
strict and sanitizer runs. `source-verified.log` confirms checked inputs stayed
unchanged through the final runs.
