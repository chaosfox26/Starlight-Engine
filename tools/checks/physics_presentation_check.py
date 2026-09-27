"""Exercise the real physics DLL: presentation must move between fixed ticks."""
import ctypes as c
import json
from pathlib import Path
import sys
import time

class Pose(c.Structure):
    _fields_ = [("id", c.c_uint32)] + [(name, c.c_float) for name in ("x", "y", "z", "qx", "qy", "qz", "qw")]

def main():
    dll_path = Path(sys.argv[1]).resolve()
    dll = c.CDLL(str(dll_path))
    dll.slp_create.restype = c.c_void_p
    dll.slp_destroy.argtypes = [c.c_void_p]
    dll.slp_terrain.argtypes = [c.c_void_p, c.POINTER(c.c_float)]
    dll.slp_ready.argtypes = [c.c_void_p]
    dll.slp_add.argtypes = [c.c_void_p, c.POINTER(c.c_float), c.c_int, c.POINTER(c.c_uint32), c.c_int, c.c_int] + [c.c_float] * 8
    dll.slp_add.restype = c.c_uint32
    dll.slp_step.argtypes = [c.c_void_p, c.c_double, c.c_void_p, c.c_int, c.POINTER(Pose), c.c_int]
    dll.slp_present.argtypes = [c.c_void_p, c.POINTER(Pose), c.c_int]
    dll.slp_ticks.argtypes = [c.c_void_p]
    dll.slp_ticks.restype = c.c_uint64
    handle = dll.slp_create()
    assert handle, "Physics creation failed"
    try:
        assert dll.slp_terrain(handle, (c.c_float * (97*97))()) == 1
        deadline = time.monotonic() + 10
        while dll.slp_ready(handle) == 0 and time.monotonic() < deadline:
            time.sleep(.001)
        assert dll.slp_ready(handle) == 1, "Terrain collision did not finish"
        points = (c.c_float * 24)(*[coordinate for y in (0, 1) for z in (-.5, .5) for x in (-.5, .5) for coordinate in (x,y,z)])
        indices = (c.c_uint32 * 3)(0,1,2)
        assert dll.slp_add(handle, points, 8, indices, 3, 1, 2700, 0, 5, 0, 0, 0, 0, 1) != 0xffffffff
        raw, visual = Pose(), Pose()
        raw_positions, visual_positions = [], []
        for frame in range(120):
            assert dll.slp_step(handle, 1/480, None, 0, c.byref(raw), 1) == 1
            assert dll.slp_present(handle, c.byref(visual), 1) == 1
            raw_positions.append(raw.y)
            visual_positions.append(visual.y)
            assert raw.y - 1e-5 <= visual.y <= 5.00001, "Presentation extrapolated beyond solved fall"
            assert abs(sum(getattr(visual,q)**2 for q in ('qx','qy','qz','qw'))-1) < 1e-5
        raw_changes = sum(a!=b for a,b in zip(raw_positions,raw_positions[1:]))
        visual_changes = sum(a!=b for a,b in zip(visual_positions,visual_positions[1:]))
        assert 13 <= raw_changes <= 15 and visual_changes > 100
        assert all(b<=a+1e-6 for a,b in zip(visual_positions,visual_positions[1:])), "Interpolated fall moved backwards"
        print(json.dumps({"pass": True, "renderSamples":120, "physicsTicks":dll.slp_ticks(handle), "rawPoseChanges":raw_changes, "presentedPoseChanges":visual_changes}))
    finally:
        dll.slp_destroy(handle)

if __name__ == '__main__':
    main()
