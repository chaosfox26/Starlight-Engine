"""Summarize recorded frame intervals; never silently discard startup or hitches."""
import argparse
import json
import math
import statistics
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    def percentile(p):
        return values[max(0, math.ceil(len(values) * p) - 1)]
    return dict(mean=statistics.mean(values), p50=percentile(.5),
                p95=percentile(.95), p99=percentile(.99), maximum=values[-1])


def summarize(path, start=0):
    frames, samples, columns = [], [], None
    ended, dropped, malformed = False, 0, 0
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                malformed += 1
                continue
            if record["type"] == "header":
                columns = record["frameColumns"]
            elif record["type"] == "sample":
                samples.append(record)
                dropped = max(dropped, record.get("droppedRecords", 0))
                frames.extend(dict(zip(columns, row)) for row in record["frames"])
            elif record["type"] == "end":
                ended = True
                dropped = max(dropped, record.get("droppedRecords", 0))
    selected = [f for f in frames if f["sessionSeconds"] >= start]
    times = sorted(f["intervalMs"] for f in selected)
    if not times:
        raise ValueError("No frame intervals in the requested window")
    def low(fraction):
        return 1000 / statistics.mean(times[-max(1, math.ceil(len(times)*fraction)):])
    sequence = [f["frame"] for f in frames]
    gaps = sum(b != a+1 for a, b in zip(sequence, sequence[1:]))
    def worker_summary(workers, scope):
        if not workers:
            return None
        slots = max((len(w["chunkMs"]) for w in workers), default=0)
        return dict(instrumentedSamples=len(workers),
            samplesWithParallelWork=sum(w["parallelBatches"] > 0 for w in workers),
            parallelBatches=sum(w["parallelBatches"] for w in workers),
            serialBatches=sum(w["serialBatches"] for w in workers),
            maximumParticipants=max(w["participants"] for w in workers),
            workItems=sum(w["items"] for w in workers),
            wallMs=distribution([w["wallMs"] for w in workers]),
            callerWaitMs=distribution([w["callerWaitMs"] for w in workers]),
            chunkTotalMs=[sum(w["chunkMs"][i] for w in workers if len(w["chunkMs"]) > i) for i in range(slots)],
            scope=scope+" Slot zero is the caller. Elapsed chunk times overlap and include preemption.")
    multicore = worker_summary([f["multicore"] for f in selected if "multicore" in f], "Renderer pool, per frame; not OS CPU utilization.")
    updates = {f["updateSequence"]: f["battleWorkers"] for f in selected if "battleWorkers" in f}
    battle = worker_summary(list(updates.values()), "Battle visual preparation, unique observed simulation updates; unobserved updates are not inferred.")
    return dict(source=str(path), completed=ended, multicore=multicore, battleWorkers=battle,
                coverage=dict(recordedFrames=len(frames), selectedFrames=len(selected),
                              startSeconds=start, droppedRecords=dropped,
                              malformedLines=malformed, frameSequenceGaps=gaps),
                averageFps=1000/statistics.mean(times), low1Fps=low(.01), low01Fps=low(.001),
                frameTimeMs=distribution(times),
                stages={key: distribution([f[key] for f in selected if f[key] >= 0])
                        for key in columns if key.endswith("Ms") and key != "intervalMs"},
                slowestFrames=sorted(selected, key=lambda f:f["intervalMs"], reverse=True)[:20],
                events=[dict(frame=f["frame"], seconds=f["sessionSeconds"], events=f.get("events", []))
                        for f in selected if f.get("events")],
                lastOverlay=samples[-1]["overlay"],
                notes="CPU render-completion intervals, not scanout. Stage measurements can overlap. "
                      "Repeated updateSequence reuses a simulation sample. Raw log preserves every overlay graph and object reading.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--start-seconds", type=float, default=0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = summarize(args.log, args.start_seconds)
    output = args.output or args.log.with_suffix(".summary.json")
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k:result[k] for k in ("averageFps","low1Fps","low01Fps","coverage")}))
