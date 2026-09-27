"""Create a combined physics acceptance scene from a standard starter file."""
import json
import sys
from pathlib import Path

source, output = map(Path, sys.argv[1:])
s = json.loads(source.read_text(encoding="utf-8"))
base = next(c for c in s["Characters"] if c.get("Shape") == "cube")
s["Name"] = "Mixed physics acceptance"
s["Rules"] = []
s["Quests"] = []
s["Game"].update(SpawnX=0, SpawnY=1.7, SpawnZ=0, ProjectileSpeed=0)
s["Terrain"]["WaterEnabled"] = True
s["Terrain"]["WaterLevel"] = .5
s["FogDensity"] = .02
s["Characters"] = []
def add(identifier, shape, **fields):
    c = dict(base)
    c.update(Id=identifier, Name=identifier, Shape=shape, Asset="", Sculpt=None,
             GameplayRole="none", GameplayKey="", X=0, Y=0, Z=0, Width=1,
             Height=1, Depth=1, Size=1, Route=[], Dynamic=False,
             CombatHealth=0, CombatAutoAttack=False, CombatPursuit=False, JointKind=0)
    c.update(fields)
    s["Characters"].append(c)
add("mixed-breakable", "cube", GameplayRole="breakable", Z=-4, Y=.5, Width=1.5, Height=1.5)
add("mixed-fire", "fire", X=3, Y=1, Z=-3)
add("mixed-rope", "cube", X=3, Y=3, Z=-4, Size=.3, Dynamic=True, JointKind=3, JointY=1, JointTargetY=2.2, JointLength=2, JointSegments=12)
add("mixed-fall", "rock", X=-3, Y=12, Z=-3, Dynamic=True, Density=1500)
add("mixed-shooter", "cube", X=0, Z=-8, RotationY=180, CombatHealth=1000, CombatAutoAttack=True, CombatProjectileSpeed=5)
add("mixed-lightning", "chaos-lightning", X=4, Y=1, Z=2, TargetX=0, TargetY=-1, TargetZ=0)
add("mixed-lava", "lava", X=6, Z=3, Y=.3)
add("mixed-grass", "grass", X=-3, Z=2)
for i in range(16):
    add("mixed-smoke-"+str(i), "shadowenemy-smoke", X=-6+(i%4)*3, Y=1, Z=3+(i//4)*2)
output.write_text(json.dumps(s, indent=2), encoding="utf-8")
