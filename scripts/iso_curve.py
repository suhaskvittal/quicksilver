"""Compare policy curves at common footprint and speedup targets."""

from math import isfinite


def _points(points):
    result = [(float(x), float(y)) for x, y in points]
    if len(result) < 2 or any(not (isfinite(x) and isfinite(y)) for x, y in result):
        raise ValueError("Each curve needs at least two finite (footprint, speedup) points")
    return sorted(result)


def _interpolate(points, target, known_axis):
    """Interpolate on measured segments, choosing the most efficient crossing."""
    known = 0 if known_axis == "footprint" else 1
    ordered = _points(points)
    candidates = [point for point in ordered if point[known] == target]
    for left, right in zip(ordered, ordered[1:]):
        start, end = left[known], right[known]
        if start == end or not min(start, end) <= target <= max(start, end):
            continue
        weight = (target - start) / (end - start)
        candidates.append((left[0] + weight * (right[0] - left[0]),
                           left[1] + weight * (right[1] - left[1])))
    if not candidates:
        raise ValueError(f"Target {target} is outside the measured {known_axis} range")
    if known_axis == "footprint":
        return max(candidates, key=lambda p: (p[1], -p[0]))
    return min(candidates, key=lambda p: (p[0], -p[1]))


def _targets(curves, axis, requested, count):
    index = 0 if axis == "footprint" else 1
    low = max(min(p[index] for p in points) for points in curves.values())
    high = min(max(p[index] for p in points) for points in curves.values())
    if requested is not None:
        targets = [float(target) for target in requested]
        if any(not isfinite(target) or not low <= target <= high for target in targets):
            raise ValueError(f"Requested {axis} target lies outside the common measured range")
        return targets, (low, high)
    if low > high:
        return [], (low, high)
    if low == high or count == 1:
        target = 2.0 if axis == "footprint" and low <= 2.0 <= high else (low + high) / 2
        return [target], (low, high)
    targets = [low + i * (high - low) / (count - 1) for i in range(count)]
    if axis == "footprint" and count == 3 and low <= 2.0 <= high:
        targets[1] = 2.0
    return targets, (low, high)


def iso_comparison(curves, *, footprints=None, speedups=None, count=3):
    """Interpolate each policy at common footprint and speedup targets.

    ``curves`` maps policy labels to (relative footprint, speedup percent) pairs.
    Default targets span the intersection of all curves' measured ranges.
    User-supplied targets must be inside that intersection. Interpolation is
    piecewise linear between measured configurations; it never extrapolates.
    """
    if not curves or count < 1:
        raise ValueError("Provide at least one curve and a positive target count")
    prepared = {name: _points(points) for name, points in curves.items()}
    footprint_targets, footprint_range = _targets(prepared, "footprint", footprints, count)
    speedup_targets, speedup_range = _targets(prepared, "speedup", speedups, count)
    return {
        "footprint_range": footprint_range,
        "speedup_range": speedup_range,
        "at_footprint": [
            (target, {name: _interpolate(points, target, "footprint")
                      for name, points in prepared.items()})
            for target in footprint_targets
        ],
        "at_speedup": [
            (target, {name: _interpolate(points, target, "speedup")
                      for name, points in prepared.items()})
            for target in speedup_targets
        ],
    }


def pairwise_iso_comparison(curves, *, reference=None, footprints=None,
                            speedups=None, count=3):
    """Compare each pair, using shared footprint targets when possible."""
    if len(curves) < 2:
        raise ValueError("Pairwise comparison needs at least two curves")
    if reference is None:
        reference = next(iter(curves))
    if reference not in curves:
        raise ValueError(f"Reference policy {reference!r} is not in the curves")
    if footprints is None:
        shared_footprints, _ = _targets(
            {name: _points(points) for name, points in curves.items()},
            "footprint", None, count,
        )
        if shared_footprints:
            footprints = shared_footprints
    results = {}
    for name, points in curves.items():
        if name == reference:
            continue
        result = iso_comparison(
            {reference: curves[reference], name: points},
            footprints=footprints, speedups=speedups, count=count,
        )
        if not result["at_speedup"]:
            result["closest_speedup_pair"] = _closest_pair(
                curves[reference], points, "speedup",
            )
        if not result["at_footprint"]:
            result["closest_footprint_pair"] = _closest_pair(
                curves[reference], points, "footprint",
            )
        results[reference, name] = result
    return results


def _closest_pair(left, right, known_axis):
    """Find the two observed samples with the smallest axis mismatch."""
    known = 0 if known_axis == "footprint" else 1
    tie_break = (lambda a, b: -(a[1] + b[1])) if known == 0 else (
        lambda a, b: a[0] + b[0]
    )
    return min(
        ((a, b) for a in _points(left) for b in _points(right)),
        key=lambda pair: (abs(pair[0][known] - pair[1][known]),
                          tie_break(*pair)),
    )


def report_iso_points(curves, title, *, reference=None, footprints=None,
                      speedups=None, count=3):
    """Print all-policy and pairwise iso comparisons; return the pairwise results.

    Call ``iso_comparison`` directly to obtain the all-policy result as data.
    """
    all_policies = iso_comparison(
        curves, footprints=footprints, speedups=speedups, count=count,
    )
    comparisons = pairwise_iso_comparison(
        curves, reference=reference, footprints=footprints,
        speedups=speedups, count=count,
    )
    print(f"\n{title} — piecewise-linear interpolation between measured points")
    print("\nAll policies")
    _print_comparison(all_policies, tuple(curves))
    for labels, result in comparisons.items():
        print(f"\n{labels[0]} vs {labels[1]}")
        _print_comparison(result, labels)
    return comparisons


def _print_comparison(result, labels):
    for kind, iso_label, target_label in (
        ("at_speedup", "speedup", "Target speedup (%)"),
        ("at_footprint", "footprint", "Target footprint (×)"),
    ):
        rows = result[kind]
        if not rows:
            low, high = result["speedup_range" if kind == "at_speedup" else "footprint_range"]
            print(f"No common {iso_label} range ({low:.2f} to {high:.2f}).")
            pair = result.get(f"closest_{iso_label}_pair")
            if pair is not None:
                gap = abs(pair[0][0 if kind == "at_footprint" else 1]
                          - pair[1][0 if kind == "at_footprint" else 1])
                unit = "×" if kind == "at_footprint" else " percentage points"
                print(f"Closest measured pair (gap {gap:.3f}{unit}): "
                      + ", ".join(f"{label} {point[0]:.3f}× / {point[1]:.3f}%"
                                  for label, point in zip(labels, pair)))
            continue
        print(f"Iso-{iso_label}: interpolated footprint × / speedup % by policy")
        print(f"  {target_label:>20}  " + "  ".join(f"{label:>24}" for label in labels))
        for target, values in rows:
            formatted = [f"{values[label][0]:.3f}× / {values[label][1]:.3f}%"
                         for label in labels]
            print(f"  {target:20.3f}  " + "  ".join(f"{value:>24}" for value in formatted))
