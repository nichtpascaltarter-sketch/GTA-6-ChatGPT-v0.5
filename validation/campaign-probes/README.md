# Campaign route evidence

These portable probes completed both craft contracts using `Game::update` inputs
from acceptance through completion. They retain all **50 initialized vehicles
and 84 pedestrians**, including population simulation and the static world. The
acceptance logic services the existing starter craft. No position, velocity,
health, stage, or completion-state writes occur after acceptance.

The setup positions the player at the appropriate contact and sets previously
completed contracts to 4 (rescue) or 5 (survey); it does not replay the preceding
campaign. The probes run a deterministic feedback controller at 1/60 second per
step. They prove these routes and deadlines are feasible in the simulation;
they do not measure real-time performance, rendering, human controls, or Windows
hardware behavior. Earlier exploratory runs cleared ambient population; **the
source and logs archived here do not**.

| Probe | Completed input time | Reward | Final craft / player health |
| --- | ---: | ---: | ---: |
| Clinic rescue and pier return | 183.867 s | $1,800 | 100 / 100 |
| Three airborne survey gates and runway stop | 193.683 s | $2,600 | 100 / 100 |

Both programs exit nonzero if acceptance/entry fails or the final completed
chapter, inactive contract, reward, or full-health assertions fail. Times count
completed input steps after entry. The survey uses approximately 45 m/s cruise
and 38 m/s approach, reducing throttle before touchdown. Its final position is
`(-3192.850, 4.000, -948.031)`. The rescue controller spends time turning near the
disabled launch before completing its return; the complete log retains that
behavior.

## Source and invocation

The archived logs were produced against committed game/world sources at
`88a2cf6e24f3c7037581142915f757d8a49fa5e0`, extracted to
`/tmp/meridian-campaign-evidence-88a2cf6`. The two probe sources in this directory
were copied into that tree at the same relative path. Compiler:
`c++ (Debian 14.2.0-19) 14.2.0`, Linux x64. No visual or third-party libraries are
linked. Binaries remain outside the repository.

Exact compile/run invocations from that extracted tree:

```sh
set -o pipefail
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror src/world.cpp src/game.cpp validation/campaign-probes/rescue_probe.cpp -o /tmp/meridian-rescue-probe
/tmp/meridian-rescue-probe | tee /workspace/GTA-6-ChatGPT-v0.5/validation/campaign-probes/rescue.log
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror src/world.cpp src/game.cpp validation/campaign-probes/survey_probe.cpp -o /tmp/meridian-survey-probe
/tmp/meridian-survey-probe | tee /workspace/GTA-6-ChatGPT-v0.5/validation/campaign-probes/survey.log
```

To recreate the source tree from this repository:

```sh
mkdir -p /tmp/meridian-campaign-evidence-88a2cf6/validation/campaign-probes
git archive 88a2cf6e24f3c7037581142915f757d8a49fa5e0 src | tar -x -C /tmp/meridian-campaign-evidence-88a2cf6
cp validation/campaign-probes/rescue_probe.cpp validation/campaign-probes/survey_probe.cpp /tmp/meridian-campaign-evidence-88a2cf6/validation/campaign-probes/
cd /tmp/meridian-campaign-evidence-88a2cf6
```
