"""
Compiler Output Analysis Utilities

This module provides functions for analyzing and aggregating compiler output files
from quantum circuit compilation experiments. The output files are typically located
in out/dms/compiled_results/ and organized by policy (e.g., hint, viszlai).

MAIN ANALYSIS FUNCTIONS:
========================

analyze_compiler_output(file_path)
    - Parses a single compiler output file (.out) and extracts statistics
    - Returns dictionary with metrics like compile_time, num_qubits, memory_instructions
    - Handles RREF histograms as nested dictionaries
    - Core function used by all other aggregation functions

get_compiler_stats_summary(stats)
    - Generates human-readable summary from analyze_compiler_output() results
    - Formats numbers with commas, shows percentages for histograms
    - Useful for quick inspection of individual file statistics

POLICY DATA AGGREGATION:
========================

get_compiler_policy_results(policy, results_dir)
    - Loads all benchmark results for a specific compiler policy (e.g., "hint", "viszlai")
    - Returns dictionary: {benchmark_name: stats_dict}
    - Main function for getting compiler policy-specific data
    - Example: get_compiler_policy_results('hint') returns all hint policy benchmarks

get_available_compiler_policies(results_dir)
    - Lists all available compiler policies in the results directory
    - Returns sorted list of policy names
    - Useful for discovering what compiler policies are available

DATA FILTERING AND SELECTION:
=============================

filter_policy_by_suffix(policy_data, suffix, remove_suffix=True)
    - Filters benchmarks by suffix (e.g., "c12_100M", "c16_100M", "c12_10M")
    - Works for both compiler and simulation policy data
    - By default removes the suffix from keys in returned dictionary
    - Takes policy dictionary and returns filtered dictionary with cleaned keys
    - Example: filter_policy_by_suffix(hint_data, 'c12_100M')
      Returns: {'grover_3sat_1710': {...}, 'shor_rsa256': {...}}
      Instead of: {'grover_3sat_1710_c12_100M': {...}, 'shor_rsa256_c12_100M': {...}}

filter_policy_by_pattern(policy_data, pattern, match_type, remove_pattern=False)
    - More flexible pattern matching for both compiler and simulation data:
      * "contains" - pattern anywhere in benchmark name
      * "starts_with" - pattern at beginning of name
      * "ends_with" - pattern at end of name (same as suffix)
      * "exact" - exact name match
    - Case-insensitive matching
    - Optional pattern removal from keys when remove_pattern=True
    - Example: filter_policy_by_pattern(data, 'grover', 'contains', remove_pattern=True)
      Converts 'grover_3sat_1710_c12_100M' to '3sat_1710_c12_100M'

COMPARISON AND ANALYSIS:
========================

compare_compiler_benchmark_across_policies(benchmark_name, policies, results_dir)
    - Compares a specific benchmark across multiple compiler policies
    - Shows side-by-side metrics like compile_time, memory_instructions, etc.
    - Includes RREF histogram comparison with percentages
    - Example: compare_compiler_benchmark_across_policies('grover_3sat_1710_c12_100M')

get_compiler_filtered_summary(filtered_data, filter_description)
    - Generates comprehensive summary of filtered compiler benchmark data
    - Shows individual benchmark details and aggregate statistics
    - Calculates min/max/average for compile times and memory usage
    - Lists unique qubit counts across benchmarks
    - Example: get_compiler_filtered_summary(c12_data, 'c12_100M benchmarks')

UTILITY AND DISCOVERY FUNCTIONS:
================================

get_compiler_benchmarks_summary(policy, results_dir)
    - Gets summary information about compiler benchmarks and policies
    - If policy=None, analyzes all compiler policies and finds common benchmarks
    - If policy specified, returns info for that specific compiler policy
    - Shows total counts and lists all benchmark names

find_compiler_benchmarks_by_pattern(pattern, policy, results_dir)
    - Finds compiler benchmarks matching a pattern across policies
    - If policy=None, searches all compiler policies
    - If policy specified, searches only that compiler policy
    - Returns dictionary mapping policy names to matching benchmark lists
    - Example: find_compiler_benchmarks_by_pattern('grover') finds all grover benchmarks

TYPICAL USAGE PATTERNS:
======================

# Basic analysis of a single compiler policy
hint_data = get_compiler_policy_results('hint')
c12_benchmarks = filter_policy_by_suffix(hint_data, 'c12_100M')  # Keys: 'grover_3sat_1710', etc.
summary = get_compiler_filtered_summary(c12_benchmarks, 'c12_100M')

# Cross-policy comparison
comparison = compare_compiler_benchmark_across_policies('grover_3sat_1710_c12_100M')

# Discovery and exploration
policies = get_available_compiler_policies()
grover_matches = find_compiler_benchmarks_by_pattern('grover')
overall_summary = get_compiler_benchmarks_summary()

SIMULATION RESULTS ANALYSIS FUNCTIONS:
======================================

analyze_simulation_output(file_path)
    - Parses a single simulation output file (.out) and extracts statistics
    - Returns dictionary with metrics like kips, application_success_rate, memory_swaps
    - Handles EPR buffer occupancy histograms as nested dictionaries
    - Core function used by all simulation aggregation functions

get_simulation_stats_summary(stats)
    - Generates human-readable summary from analyze_simulation_output() results
    - Shows performance, error rates, resource usage, and histogram summaries
    - Useful for quick inspection of individual simulation results

SIMULATION POLICY DATA AGGREGATION:
===================================

get_simulation_policy_results(policy, results_dir)
    - Loads all benchmark results for a specific simulation policy
    - Supports policies like "hint", "viszlai", "baseline", "hint_c", "hint_epr16", etc.
    - Returns dictionary: {benchmark_name: stats_dict}
    - Main function for getting simulation policy-specific data

get_available_simulation_policies(results_dir)
    - Lists all available simulation policies in the results directory
    - Returns sorted list of policy names
    - Useful for discovering what simulation policies are available

SIMULATION DATA FILTERING AND SELECTION:
========================================

# Note: Simulation filtering now uses the unified filter_policy_by_suffix and filter_policy_by_pattern functions

SIMULATION COMPARISON AND ANALYSIS:
===================================

compare_simulation_benchmark_across_policies(benchmark_name, policies, results_dir)
    - Compares a specific benchmark across multiple simulation policies
    - Shows side-by-side metrics like KIPS, success rates, resource usage
    - Includes EPR buffer histogram comparison with percentages
    - Example: compare_simulation_benchmark_across_policies('grover_3sat_1710_c12_10M')

get_simulation_filtered_summary(filtered_data, filter_description)
    - Generates comprehensive summary of filtered simulation benchmark data
    - Shows individual benchmark details and aggregate statistics
    - Calculates min/max/average for KIPS, success rates, memory swaps, total qubits
    - Example: get_simulation_filtered_summary(c12_10m_data, 'c12_10M benchmarks')

FILE FORMATS:
=============

COMPILER OUTPUT FILES (.out in out/dms/compiled_results/):
- Progress lines with [ MEMOPT ] prefix (skipped during parsing)
- Final statistics as KEY VALUE pairs (e.g., "COMPILE_TIME 109.54346900")
- RREF_HISTOGRAM section with indented RREF=N COUNT entries
- All numeric values are parsed as int or float automatically
- Histograms are returned as nested dictionaries {rref_value: count}

SIMULATION OUTPUT FILES (.out in out/dms/simulation_results/):
- Slurm job headers (skipped during parsing)
- Progress lines with CLIENT 0 @ updates (skipped during parsing)
- SIMULATION_STATS section with KEY : VALUE pairs
- EPR_BUFFER_OCCU_HISTOGRAM section with range entries
- All numeric values are parsed as int or float automatically
- EPR histograms are returned as nested dictionaries {range_key: count}
"""

import re
import os
from typing import Dict, Any, Union


def analyze_compiler_output(file_path: str) -> Dict[str, Any]:
    """
    Analyze a compiler output file and return a dictionary of statistics.

    Args:
        file_path: Path to the compiler output file

    Returns:
        Dictionary containing parsed statistics. For histograms, the value is another dictionary.

    Example:
        {
            'num_qubits': 241,
            'inst_done': 5708197,
            'unrolled_inst_done': 120003297,
            'memory_instructions': 151223,
            'memory_prefetches': 0,
            'emission_calls': 27428,
            'total_timesteps': 54874,
            'compile_time': 109.54346900,
            'compute_intensity': 793.55188695,
            'mean_rref_interval': 36.93931045,
            'near_immediate_rref': 84409,
            'rref_histogram': {
                1: 38630,
                2: 22131,
                3: 14721,
                4: 8927,
                5: 7092,
                6: 4509,
                7: 4515,
                8: 50473
            }
        }
    """
    stats = {}

    try:
        with open(file_path, 'r') as f:
            lines = f.readlines()
    except FileNotFoundError:
        raise FileNotFoundError(f"Compiler output file not found: {file_path}")
    except Exception as e:
        raise Exception(f"Error reading file {file_path}: {str(e)}")

    # Parse the file line by line
    in_histogram = False
    histogram_data = {}

    for line in lines:
        line = line.strip()

        # Skip empty lines
        if not line:
            continue

        # Extract number of qubits from MEMOPT lines
        if line.startswith('[ MEMOPT ] num qubits:'):
            match = re.search(r'num qubits:\s*(\d+)', line)
            if match:
                stats['num_qubits'] = int(match.group(1))

        # Skip other MEMOPT progress lines
        elif line.startswith('[ MEMOPT ] progress:'):
            continue

        # Check if we're entering the histogram section
        elif line == 'RREF_HISTOGRAM':
            in_histogram = True
            continue

        # Parse histogram entries
        elif in_histogram:
            # Check for indented RREF entries (may start with tabs/spaces)
            if 'RREF=' in line:
                match = re.search(r'RREF=(\d+)\s+(\d+)', line)
                if match:
                    rref_value = int(match.group(1))
                    count = int(match.group(2))
                    histogram_data[rref_value] = count
            else:
                # End of histogram section
                in_histogram = False
                if histogram_data:
                    stats['rref_histogram'] = histogram_data

        # Parse other statistics (key-value pairs)
        elif not in_histogram and not line.startswith('[ MEMOPT ]'):
            # Look for lines with statistics in format: KEY    VALUE
            parts = line.split()
            if len(parts) >= 2:
                key = parts[0].lower()
                try:
                    # Try to parse as number (int or float)
                    value_str = parts[-1]  # Take the last part as the value

                    # Try integer first
                    if '.' not in value_str:
                        value = int(value_str)
                    else:
                        value = float(value_str)

                    stats[key] = value

                except ValueError:
                    # If parsing as number fails, store as string
                    stats[key] = parts[-1]

    # Add histogram data if we collected any
    if histogram_data:
        stats['rref_histogram'] = histogram_data

    return stats


def get_compiler_stats_summary(stats: Dict[str, Any]) -> str:
    """
    Generate a human-readable summary of compiler statistics.

    Args:
        stats: Dictionary returned by analyze_compiler_output()

    Returns:
        Formatted string summary of the statistics
    """
    summary_lines = []

    # Basic info
    if 'num_qubits' in stats:
        summary_lines.append(f"Number of qubits: {stats['num_qubits']:,}")

    # Performance metrics
    if 'compile_time' in stats:
        summary_lines.append(f"Compile time: {stats['compile_time']:.2f} seconds")

    if 'inst_done' in stats:
        summary_lines.append(f"Instructions processed: {stats['inst_done']:,}")

    if 'unrolled_inst_done' in stats:
        summary_lines.append(f"Unrolled instructions: {stats['unrolled_inst_done']:,}")

    if 'memory_instructions' in stats:
        summary_lines.append(f"Memory instructions: {stats['memory_instructions']:,}")

    if 'compute_intensity' in stats:
        summary_lines.append(f"Compute intensity: {stats['compute_intensity']:.2f}")

    # RREF statistics
    if 'mean_rref_interval' in stats:
        summary_lines.append(f"Mean RREF interval: {stats['mean_rref_interval']:.2f}")

    if 'near_immediate_rref' in stats:
        summary_lines.append(f"Near immediate RREF: {stats['near_immediate_rref']:,}")

    # Histogram summary
    if 'rref_histogram' in stats:
        histogram = stats['rref_histogram']
        total_entries = sum(histogram.values())
        summary_lines.append(f"RREF histogram entries: {total_entries:,}")

        # Show top 3 RREF values by count
        sorted_rref = sorted(histogram.items(), key=lambda x: x[1], reverse=True)
        top_3 = sorted_rref[:3]
        summary_lines.append("Top RREF values:")
        for rref_val, count in top_3:
            percentage = (count / total_entries) * 100
            summary_lines.append(f"  RREF={rref_val}: {count:,} ({percentage:.1f}%)")

    return "\n".join(summary_lines)


def get_compiler_policy_results(policy: str, results_dir: str = "out/dms/compiled_results") -> Dict[str, Dict[str, Any]]:
    """
    Get compiler output data for a specific policy.

    Args:
        policy: Name of the compiler policy (e.g., "hint", "viszlai")
        results_dir: Path to the compiled results directory

    Returns:
        Dictionary with structure:
        {
            "benchmark_name": {
                "num_qubits": ...,
                "compile_time": ...,
                "rref_histogram": {...},
                ...
            }
        }

    Example:
        {
            "grover_3sat_1710_c12_100M": {
                "num_qubits": 1710,
                "compile_time": 204.86,
                "rref_histogram": {1: 10184, 2: 1907, ...},
                ...
            },
            "c2h4o_ethylene_oxide_240_c16_100M": {...},
            ...
        }
    """
    if not os.path.exists(results_dir):
        raise FileNotFoundError(f"Results directory not found: {results_dir}")

    policy_path = os.path.join(results_dir, policy)

    if not os.path.exists(policy_path):
        available_policies = [d for d in os.listdir(results_dir)
                             if os.path.isdir(os.path.join(results_dir, d))]
        raise ValueError(f"Policy '{policy}' not found. Available policies: {available_policies}")

    if not os.path.isdir(policy_path):
        raise ValueError(f"Policy path '{policy_path}' is not a directory")

    policy_data = {}

    # Get all .out files in the policy directory
    out_files = [f for f in os.listdir(policy_path) if f.endswith('.out')]

    if not out_files:
        print(f"Warning: No .out files found in {policy_path}")
        return policy_data

    for out_file in out_files:
        # Extract benchmark name (remove .out extension)
        benchmark_name = out_file[:-4]  # Remove '.out'

        file_path = os.path.join(policy_path, out_file)

        try:
            # Analyze the file and store the results
            stats = analyze_compiler_output(file_path)
            policy_data[benchmark_name] = stats

        except Exception as e:
            print(f"Warning: Failed to parse {file_path}: {str(e)}")
            # Store error information instead of skipping
            policy_data[benchmark_name] = {
                "error": str(e),
                "file_path": file_path
            }

    return policy_data


def get_available_compiler_policies(results_dir: str = "out/dms/compiled_results") -> list:
    """
    Get list of available compiler policies in the results directory.

    Args:
        results_dir: Path to the compiled results directory

    Returns:
        List of compiler policy names
    """
    if not os.path.exists(results_dir):
        raise FileNotFoundError(f"Results directory not found: {results_dir}")

    policies = [d for d in os.listdir(results_dir)
                if os.path.isdir(os.path.join(results_dir, d))]

    return sorted(policies)


def compare_compiler_benchmark_across_policies(benchmark_name: str,
                                             policies: list = None,
                                             results_dir: str = "out/dms/compiled_results") -> str:
    """
    Generate a comparison summary for a specific benchmark across compiler policies.

    Args:
        benchmark_name: Name of the benchmark to compare
        policies: List of compiler policies to compare (if None, compares all available policies)
        results_dir: Path to the compiled results directory

    Returns:
        Formatted string comparing the benchmark across compiler policies
    """
    if policies is None:
        policies = get_available_compiler_policies(results_dir)

    summary_lines = [f"Comparison for benchmark: {benchmark_name}"]
    summary_lines.append("=" * (len(summary_lines[0])))

    # Load data for each policy
    policy_data = {}
    available_policies = []

    for policy in policies:
        try:
            data = get_compiler_policy_results(policy, results_dir)
            if benchmark_name in data:
                policy_data[policy] = data[benchmark_name]
                available_policies.append(policy)
        except (ValueError, FileNotFoundError) as e:
            print(f"Warning: Could not load policy '{policy}': {str(e)}")

    if not available_policies:
        return f"Benchmark '{benchmark_name}' not found in any of the specified policies: {policies}"

    # Compare key metrics across policies
    metrics_to_compare = [
        'num_qubits', 'compile_time', 'inst_done', 'unrolled_inst_done',
        'memory_instructions', 'compute_intensity', 'mean_rref_interval',
        'near_immediate_rref'
    ]

    for metric in metrics_to_compare:
        summary_lines.append(f"\n{metric.upper().replace('_', ' ')}:")

        for policy in available_policies:
            benchmark_data = policy_data[policy]

            if 'error' in benchmark_data:
                summary_lines.append(f"  {policy}: ERROR - {benchmark_data['error']}")
            elif metric in benchmark_data:
                value = benchmark_data[metric]
                if isinstance(value, float):
                    summary_lines.append(f"  {policy}: {value:.2f}")
                elif isinstance(value, int):
                    summary_lines.append(f"  {policy}: {value:,}")
                else:
                    summary_lines.append(f"  {policy}: {value}")
            else:
                summary_lines.append(f"  {policy}: N/A")

    # Compare RREF histograms if available
    summary_lines.append(f"\nRREF HISTOGRAM COMPARISON:")
    for policy in available_policies:
        benchmark_data = policy_data[policy]

        if 'error' in benchmark_data:
            summary_lines.append(f"  {policy}: ERROR")
        elif 'rref_histogram' in benchmark_data:
            histogram = benchmark_data['rref_histogram']
            total_entries = sum(histogram.values())
            summary_lines.append(f"  {policy}: {total_entries:,} total entries")

            # Show top 3 RREF values
            sorted_rref = sorted(histogram.items(), key=lambda x: x[1], reverse=True)
            for rref_val, count in sorted_rref[:3]:
                percentage = (count / total_entries) * 100
                summary_lines.append(f"    RREF={rref_val}: {count:,} ({percentage:.1f}%)")
        else:
            summary_lines.append(f"  {policy}: No histogram data")

    return "\n".join(summary_lines)


def get_compiler_benchmarks_summary(policy: str = None, results_dir: str = "out/dms/compiled_results") -> Dict[str, Any]:
    """
    Get summary information about available compiler benchmarks and policies.

    Args:
        policy: Specific compiler policy to analyze (if None, analyzes all policies)
        results_dir: Path to the compiled results directory

    Returns:
        Dictionary with compiler policies, benchmarks, and summary statistics
    """
    if policy is None:
        # Analyze all policies
        policies = get_available_compiler_policies(results_dir)

        # Get all unique benchmark names across all policies
        all_benchmarks = set()
        policy_counts = {}

        for pol in policies:
            try:
                pol_data = get_compiler_policy_results(pol, results_dir)
                all_benchmarks.update(pol_data.keys())
                policy_counts[pol] = len(pol_data)
            except Exception as e:
                print(f"Warning: Could not load policy '{pol}': {str(e)}")
                policy_counts[pol] = 0

        benchmark_names = sorted(list(all_benchmarks))

        # Find benchmarks that exist in all policies
        common_benchmarks = set(benchmark_names)
        for pol in policies:
            try:
                pol_data = get_compiler_policy_results(pol, results_dir)
                common_benchmarks &= set(pol_data.keys())
            except Exception:
                common_benchmarks = set()  # If any policy fails, no common benchmarks

        return {
            'policies': policies,
            'total_policies': len(policies),
            'benchmark_names': benchmark_names,
            'total_benchmarks': len(benchmark_names),
            'policy_counts': policy_counts,
            'common_benchmarks': sorted(list(common_benchmarks)),
            'total_common_benchmarks': len(common_benchmarks)
        }
    else:
        # Analyze specific policy
        pol_data = get_compiler_policy_results(policy, results_dir)
        benchmark_names = sorted(list(pol_data.keys()))

        return {
            'policy': policy,
            'benchmark_names': benchmark_names,
            'total_benchmarks': len(benchmark_names)
        }


def find_compiler_benchmarks_by_pattern(pattern: str,
                                       policy: str = None,
                                       results_dir: str = "out/dms/compiled_results") -> Dict[str, list]:
    """
    Find compiler benchmarks matching a specific pattern.

    Args:
        pattern: String pattern to search for in benchmark names
        policy: Specific compiler policy to search in (if None, searches all policies)
        results_dir: Path to the compiled results directory

    Returns:
        Dictionary mapping compiler policy names to lists of matching benchmark names
    """
    matching_benchmarks = {}

    if policy is None:
        # Search across all policies
        policies = get_available_compiler_policies(results_dir)
    else:
        policies = [policy]

    for pol in policies:
        try:
            pol_data = get_compiler_policy_results(pol, results_dir)
            matches = [name for name in pol_data.keys() if pattern.lower() in name.lower()]
            if matches:
                matching_benchmarks[pol] = sorted(matches)
        except Exception as e:
            print(f"Warning: Could not search policy '{pol}': {str(e)}")

    return matching_benchmarks


def filter_policy_by_suffix(policy_data: Dict[str, Dict[str, Any]], suffix: str,
                           remove_suffix: bool = True) -> Dict[str, Dict[str, Any]]:
    """
    Filter a policy's dictionary to only include benchmarks with a specific suffix.
    Works for both compiler and simulation policy data.

    Args:
        policy_data: Dictionary returned by get_compiler_policy_results() or get_simulation_policy_results()
        suffix: Suffix pattern to filter by (e.g., "c12_100M", "c16_100M", "c12_10M")
        remove_suffix: If True, removes the suffix from the keys in the returned dictionary

    Returns:
        New dictionary containing only benchmarks whose names end with the specified suffix.
        If remove_suffix=True, keys will have the suffix removed.

    Example:
        hint_data = get_compiler_policy_results('hint')
        c12_benchmarks = filter_policy_by_suffix(hint_data, 'c12_100M')
        # Returns: {'grover_3sat_1710': {...}, 'c2h4o_ethylene_oxide_240': {...}, ...}
        # Instead of: {'grover_3sat_1710_c12_100M': {...}, ...}
    """
    filtered_data = {}

    for benchmark_name, benchmark_stats in policy_data.items():
        if benchmark_name.endswith(suffix):
            if remove_suffix:
                # Remove the suffix from the key
                new_key = benchmark_name[:-len(suffix)]
                # Remove trailing underscore if present
                if new_key.endswith('_'):
                    new_key = new_key[:-1]
                filtered_data[new_key] = benchmark_stats
            else:
                filtered_data[benchmark_name] = benchmark_stats

    return filtered_data


def filter_policy_by_pattern(policy_data: Dict[str, Dict[str, Any]], pattern: str,
                            match_type: str = "contains", remove_pattern: bool = False) -> Dict[str, Dict[str, Any]]:
    """
    Filter a policy's dictionary to only include benchmarks matching a pattern.
    Works for both compiler and simulation policy data.

    Args:
        policy_data: Dictionary returned by get_compiler_policy_results() or get_simulation_policy_results()
        pattern: Pattern to search for in benchmark names
        match_type: Type of matching - "contains", "starts_with", "ends_with", or "exact"
        remove_pattern: If True, removes the pattern from the keys in the returned dictionary

    Returns:
        New dictionary containing only benchmarks whose names match the pattern.
        If remove_pattern=True, the pattern will be removed from the keys.

    Example:
        hint_data = get_compiler_policy_results('hint')
        grover_benchmarks = filter_policy_by_pattern(hint_data, 'grover', 'contains')
        c12_benchmarks = filter_policy_by_pattern(hint_data, 'c12_100M', 'ends_with', remove_pattern=True)
        # With remove_pattern=True and ends_with, 'grover_3sat_1710_c12_100M' becomes 'grover_3sat_1710'
    """
    filtered_data = {}
    pattern_lower = pattern.lower()

    for benchmark_name, benchmark_stats in policy_data.items():
        benchmark_lower = benchmark_name.lower()

        match = False
        if match_type == "contains":
            match = pattern_lower in benchmark_lower
        elif match_type == "starts_with":
            match = benchmark_lower.startswith(pattern_lower)
        elif match_type == "ends_with":
            match = benchmark_lower.endswith(pattern_lower)
        elif match_type == "exact":
            match = benchmark_lower == pattern_lower
        else:
            raise ValueError(f"Invalid match_type: {match_type}. Must be one of: contains, starts_with, ends_with, exact")

        if match:
            if remove_pattern:
                new_key = benchmark_name
                if match_type == "starts_with":
                    # Remove pattern from the beginning
                    if benchmark_lower.startswith(pattern_lower):
                        new_key = benchmark_name[len(pattern):]
                        # Remove leading underscore if present
                        if new_key.startswith('_'):
                            new_key = new_key[1:]
                elif match_type == "ends_with":
                    # Remove pattern from the end
                    if benchmark_lower.endswith(pattern_lower):
                        new_key = benchmark_name[:-len(pattern)]
                        # Remove trailing underscore if present
                        if new_key.endswith('_'):
                            new_key = new_key[:-1]
                elif match_type == "contains":
                    # For contains, remove the first occurrence of the pattern
                    # Find the actual case-sensitive position
                    lower_pos = benchmark_lower.find(pattern_lower)
                    if lower_pos != -1:
                        new_key = benchmark_name[:lower_pos] + benchmark_name[lower_pos + len(pattern):]
                        # Clean up any double underscores that might result
                        new_key = new_key.replace('__', '_').strip('_')
                elif match_type == "exact":
                    # For exact match, the key would be empty, so use the pattern itself
                    new_key = pattern

                # Ensure we don't have an empty key
                if not new_key:
                    new_key = "unnamed"

                filtered_data[new_key] = benchmark_stats
            else:
                filtered_data[benchmark_name] = benchmark_stats

    return filtered_data


def get_compiler_filtered_summary(filtered_data: Dict[str, Dict[str, Any]],
                                 filter_description: str = "filtered") -> str:
    """
    Generate a summary of filtered compiler benchmark data.

    Args:
        filtered_data: Dictionary returned by filter_policy_by_suffix() or filter_policy_by_pattern()
        filter_description: Description of the filter applied (for display purposes)

    Returns:
        Formatted string summary of the filtered compiler data
    """
    if not filtered_data:
        return f"No benchmarks found matching the {filter_description} criteria."

    summary_lines = [f"Summary of {filter_description} benchmarks ({len(filtered_data)} total):"]
    summary_lines.append("=" * len(summary_lines[0]))

    # Calculate aggregate statistics
    total_compile_time = 0
    total_memory_instructions = 0
    total_qubits = 0
    compile_times = []
    memory_counts = []
    qubit_counts = []

    for benchmark_name, stats in filtered_data.items():
        if 'error' not in stats:
            if 'compile_time' in stats:
                compile_times.append(stats['compile_time'])
                total_compile_time += stats['compile_time']
            if 'memory_instructions' in stats:
                memory_counts.append(stats['memory_instructions'])
                total_memory_instructions += stats['memory_instructions']
            if 'num_qubits' in stats:
                qubit_counts.append(stats['num_qubits'])
                total_qubits += stats['num_qubits']

    # Display individual benchmarks
    summary_lines.append("\nIndividual benchmarks:")
    for benchmark_name, stats in sorted(filtered_data.items()):
        if 'error' in stats:
            summary_lines.append(f"  {benchmark_name}: ERROR - {stats['error']}")
        else:
            compile_time = stats.get('compile_time', 'N/A')
            num_qubits = stats.get('num_qubits', 'N/A')
            memory_inst = stats.get('memory_instructions', 'N/A')

            if isinstance(compile_time, (int, float)):
                compile_time = f"{compile_time:.2f}s"
            if isinstance(memory_inst, int):
                memory_inst = f"{memory_inst:,}"

            summary_lines.append(f"  {benchmark_name}: {num_qubits} qubits, {compile_time}, {memory_inst} mem_inst")

    # Display aggregate statistics
    if compile_times:
        summary_lines.append(f"\nAggregate statistics:")
        summary_lines.append(f"  Total compile time: {total_compile_time:.2f}s")
        summary_lines.append(f"  Average compile time: {sum(compile_times)/len(compile_times):.2f}s")
        summary_lines.append(f"  Min compile time: {min(compile_times):.2f}s")
        summary_lines.append(f"  Max compile time: {max(compile_times):.2f}s")

    if memory_counts:
        summary_lines.append(f"  Total memory instructions: {total_memory_instructions:,}")
        summary_lines.append(f"  Average memory instructions: {sum(memory_counts)//len(memory_counts):,}")
        summary_lines.append(f"  Min memory instructions: {min(memory_counts):,}")
        summary_lines.append(f"  Max memory instructions: {max(memory_counts):,}")

    if qubit_counts:
        unique_qubits = sorted(set(qubit_counts))
        summary_lines.append(f"  Qubit counts: {unique_qubits}")

    return "\n".join(summary_lines)


# ============================================================================
# SIMULATION RESULTS ANALYSIS FUNCTIONS
# ============================================================================

def analyze_simulation_output(file_path: str) -> Dict[str, Any]:
    """
    Analyze a simulation output file and return a dictionary of statistics.

    Args:
        file_path: Path to the simulation output file

    Returns:
        Dictionary containing parsed simulation statistics including performance metrics,
        error rates, and EPR buffer occupancy histogram.

    Example:
        {
            'kips': 10.8342,
            'virtual_inst_done': 463881,
            'unrolled_inst_done': 10000174,
            'memory_swaps': 12777,
            'application_success_rate': 0.955417,
            'epr_buffer_histogram': {
                '0_1': 3351,
                '1_2': 4449,
                ...
            },
            'compute_total_physical_qubits': 10572,
            'factory_total_physical_qubits': 24597,
            'memory_total_physical_qubits': 11520,
            ...
        }
    """
    stats = {}

    try:
        with open(file_path, 'r') as f:
            lines = f.readlines()
    except FileNotFoundError:
        raise FileNotFoundError(f"Simulation output file not found: {file_path}")
    except Exception as e:
        raise Exception(f"Error reading file {file_path}: {str(e)}")

    # Parse the file line by line
    in_histogram = False
    histogram_data = {}

    for line in lines:
        line = line.strip()

        # Skip empty lines and Slurm headers
        if not line or line.startswith('-------') or line.startswith('Begin Slurm') or line.startswith('Job ID') or line.startswith('User ID') or line.startswith('Account') or line.startswith('Resources') or line.startswith('Partition') or line.startswith('QOS') or line.startswith('Nodes') or line.startswith('Rsrc Used'):
            continue

        # Skip progress lines and iteration headers
        if line.startswith('CLIENT 0 @') or line.startswith('----------') or line.startswith('compute cycle') or line.startswith('simulated execution') or line.startswith('instruction rate'):
            continue

        # Skip section headers
        if line in ['SIMULATION_STATS------------------------------------------------------------', 'CLIENT_0', 'FACTORY_L0', 'FACTORY_L1', 'MEMORY', 'NEXT_ITERATION']:
            continue

        # Check if we're entering the EPR buffer histogram section
        if line == 'EPR_BUFFER_OCCU_HISTOGRAM':
            in_histogram = True
            continue

        # Parse histogram entries
        if in_histogram:
            if line.startswith('EPR_BUFFER_OCCU_HISTOGRAM_'):
                # Extract histogram entry: EPR_BUFFER_OCCU_HISTOGRAM_0_1 : 3351
                parts = line.split(':')
                if len(parts) == 2:
                    key_part = parts[0].strip()
                    value_part = parts[1].strip()

                    # Extract the range (e.g., "0_1" from "EPR_BUFFER_OCCU_HISTOGRAM_0_1")
                    range_match = re.search(r'EPR_BUFFER_OCCU_HISTOGRAM_(\d+_\d+)', key_part)
                    if range_match:
                        range_key = range_match.group(1)
                        try:
                            count = int(value_part)
                            histogram_data[range_key] = count
                        except ValueError:
                            pass
            else:
                # End of histogram section
                in_histogram = False
                if histogram_data:
                    stats['epr_buffer_histogram'] = histogram_data

        # Parse other statistics (key-value pairs with colon separator)
        elif not in_histogram and ':' in line:
            parts = line.split(':', 1)
            if len(parts) == 2:
                key = parts[0].strip().lower().replace(' ', '_')
                value_str = parts[1].strip()

                try:
                    # Try to parse as number (int or float)
                    if '.' in value_str or 'e' in value_str.lower():
                        value = float(value_str)
                    else:
                        value = int(value_str)

                    stats[key] = value

                except ValueError:
                    # If parsing as number fails, store as string
                    stats[key] = value_str

    # Add histogram data if we collected any
    if histogram_data:
        stats['epr_buffer_histogram'] = histogram_data

    return stats


def get_simulation_stats_summary(stats: Dict[str, Any]) -> str:
    """
    Generate a human-readable summary of simulation statistics.

    Args:
        stats: Dictionary returned by analyze_simulation_output()

    Returns:
        Formatted string summary of the simulation statistics
    """
    summary_lines = []

    # Performance metrics
    if 'kips' in stats:
        summary_lines.append(f"Performance: {stats['kips']:.2f} KIPS")

    if 'virtual_inst_done' in stats:
        summary_lines.append(f"Virtual instructions: {stats['virtual_inst_done']:,}")

    if 'unrolled_inst_done' in stats:
        summary_lines.append(f"Unrolled instructions: {stats['unrolled_inst_done']:,}")

    if 'memory_swaps' in stats:
        summary_lines.append(f"Memory swaps: {stats['memory_swaps']:,}")

    # Error rates and success
    if 'application_success_rate' in stats:
        success_pct = stats['application_success_rate'] * 100
        summary_lines.append(f"Application success rate: {success_pct:.2f}%")

    if 't_total_error' in stats:
        summary_lines.append(f"T gate total error: {stats['t_total_error']:.2e}")

    # Resource usage
    if 'compute_total_physical_qubits' in stats:
        summary_lines.append(f"Compute qubits: {stats['compute_total_physical_qubits']:,}")

    if 'factory_total_physical_qubits' in stats:
        summary_lines.append(f"Factory qubits: {stats['factory_total_physical_qubits']:,}")

    if 'memory_total_physical_qubits' in stats:
        summary_lines.append(f"Memory qubits: {stats['memory_total_physical_qubits']:,}")

    # Total qubits
    total_qubits = 0
    for key in ['compute_total_physical_qubits', 'factory_total_physical_qubits', 'memory_total_physical_qubits']:
        if key in stats:
            total_qubits += stats[key]
    if total_qubits > 0:
        summary_lines.append(f"Total physical qubits: {total_qubits:,}")

    # EPR buffer histogram summary
    if 'epr_buffer_histogram' in stats:
        histogram = stats['epr_buffer_histogram']
        total_entries = sum(histogram.values())
        summary_lines.append(f"EPR buffer histogram entries: {total_entries:,}")

        # Show top 3 occupancy ranges by count
        sorted_ranges = sorted(histogram.items(), key=lambda x: x[1], reverse=True)
        top_3 = sorted_ranges[:3]
        summary_lines.append("Top EPR buffer occupancy ranges:")
        for range_key, count in top_3:
            percentage = (count / total_entries) * 100
            summary_lines.append(f"  {range_key}: {count:,} ({percentage:.1f}%)")

    return "\n".join(summary_lines)


def get_simulation_policy_results(policy: str, results_dir: str = "out/dms/simulation_results") -> Dict[str, Dict[str, Any]]:
    """
    Get simulation output data for a specific policy.

    Args:
        policy: Name of the policy (e.g., "hint", "viszlai", "baseline", "hint_c", "hint_epr16")
        results_dir: Path to the simulation results directory

    Returns:
        Dictionary with structure:
        {
            "benchmark_name": {
                "kips": ...,
                "application_success_rate": ...,
                "epr_buffer_histogram": {...},
                ...
            }
        }

    Example:
        {
            "grover_3sat_1710_c12_10M": {
                "kips": 10.8342,
                "application_success_rate": 0.955417,
                "epr_buffer_histogram": {"0_1": 3351, "1_2": 4449, ...},
                ...
            },
            ...
        }
    """
    if not os.path.exists(results_dir):
        raise FileNotFoundError(f"Results directory not found: {results_dir}")

    policy_path = os.path.join(results_dir, policy)

    if not os.path.exists(policy_path):
        available_policies = [d for d in os.listdir(results_dir)
                             if os.path.isdir(os.path.join(results_dir, d))]
        raise ValueError(f"Policy '{policy}' not found. Available policies: {available_policies}")

    if not os.path.isdir(policy_path):
        raise ValueError(f"Policy path '{policy_path}' is not a directory")

    policy_data = {}

    # Get all .out files in the policy directory
    out_files = [f for f in os.listdir(policy_path) if f.endswith('.out')]

    if not out_files:
        print(f"Warning: No .out files found in {policy_path}")
        return policy_data

    for out_file in out_files:
        # Extract benchmark name (remove .out extension)
        benchmark_name = out_file[:-4]  # Remove '.out'

        file_path = os.path.join(policy_path, out_file)

        try:
            # Analyze the file and store the results
            stats = analyze_simulation_output(file_path)
            if stats:  # Only add if we got some data
                policy_data[benchmark_name] = stats

        except Exception as e:
            print(f"Warning: Failed to parse {file_path}: {str(e)}")
            # Store error information instead of skipping
            policy_data[benchmark_name] = {
                "error": str(e),
                "file_path": file_path
            }

    return policy_data


def get_available_simulation_policies(results_dir: str = "out/dms/simulation_results") -> list:
    """
    Get list of available simulation policies in the results directory.

    Args:
        results_dir: Path to the simulation results directory

    Returns:
        List of policy names
    """
    if not os.path.exists(results_dir):
        raise FileNotFoundError(f"Results directory not found: {results_dir}")

    policies = [d for d in os.listdir(results_dir)
                if os.path.isdir(os.path.join(results_dir, d))]

    return sorted(policies)


# Note: filter_simulation_policy_by_suffix and filter_simulation_policy_by_pattern
# have been merged into the generic filter_policy_by_suffix and filter_policy_by_pattern functions above


def compare_simulation_benchmark_across_policies(benchmark_name: str,
                                                policies: list = None,
                                                results_dir: str = "out/dms/simulation_results") -> str:
    """
    Generate a comparison summary for a specific benchmark across simulation policies.

    Args:
        benchmark_name: Name of the benchmark to compare
        policies: List of policies to compare (if None, compares all available policies)
        results_dir: Path to the simulation results directory

    Returns:
        Formatted string comparing the benchmark across policies
    """
    if policies is None:
        policies = get_available_simulation_policies(results_dir)

    summary_lines = [f"Simulation comparison for benchmark: {benchmark_name}"]
    summary_lines.append("=" * (len(summary_lines[0])))

    # Load data for each policy
    policy_data = {}
    available_policies = []

    for policy in policies:
        try:
            data = get_simulation_policy_results(policy, results_dir)
            if benchmark_name in data:
                policy_data[policy] = data[benchmark_name]
                available_policies.append(policy)
        except (ValueError, FileNotFoundError) as e:
            print(f"Warning: Could not load policy '{policy}': {str(e)}")

    if not available_policies:
        return f"Benchmark '{benchmark_name}' not found in any of the specified policies: {policies}"

    # Compare key metrics across policies
    metrics_to_compare = [
        'kips', 'virtual_inst_done', 'unrolled_inst_done', 'memory_swaps',
        'application_success_rate', 't_total_error', 'compute_total_physical_qubits',
        'factory_total_physical_qubits', 'memory_total_physical_qubits'
    ]

    for metric in metrics_to_compare:
        summary_lines.append(f"\n{metric.upper().replace('_', ' ')}:")

        for policy in available_policies:
            benchmark_data = policy_data[policy]

            if 'error' in benchmark_data:
                summary_lines.append(f"  {policy}: ERROR - {benchmark_data['error']}")
            elif metric in benchmark_data:
                value = benchmark_data[metric]
                if isinstance(value, float):
                    if metric == 'application_success_rate':
                        summary_lines.append(f"  {policy}: {value*100:.2f}%")
                    elif 'error' in metric:
                        summary_lines.append(f"  {policy}: {value:.2e}")
                    else:
                        summary_lines.append(f"  {policy}: {value:.2f}")
                elif isinstance(value, int):
                    summary_lines.append(f"  {policy}: {value:,}")
                else:
                    summary_lines.append(f"  {policy}: {value}")
            else:
                summary_lines.append(f"  {policy}: N/A")

    # Compare EPR buffer histograms if available
    summary_lines.append(f"\nEPR BUFFER HISTOGRAM COMPARISON:")
    for policy in available_policies:
        benchmark_data = policy_data[policy]

        if 'error' in benchmark_data:
            summary_lines.append(f"  {policy}: ERROR")
        elif 'epr_buffer_histogram' in benchmark_data:
            histogram = benchmark_data['epr_buffer_histogram']
            total_entries = sum(histogram.values())
            summary_lines.append(f"  {policy}: {total_entries:,} total entries")

            # Show top 3 EPR buffer ranges
            sorted_ranges = sorted(histogram.items(), key=lambda x: x[1], reverse=True)
            for range_key, count in sorted_ranges[:3]:
                percentage = (count / total_entries) * 100
                summary_lines.append(f"    {range_key}: {count:,} ({percentage:.1f}%)")
        else:
            summary_lines.append(f"  {policy}: No histogram data")

    return "\n".join(summary_lines)


def get_simulation_filtered_summary(filtered_data: Dict[str, Dict[str, Any]],
                                   filter_description: str = "filtered") -> str:
    """
    Generate a summary of filtered simulation benchmark data.

    Args:
        filtered_data: Dictionary returned by filter_policy_by_suffix() or filter_policy_by_pattern()
        filter_description: Description of the filter applied (for display purposes)

    Returns:
        Formatted string summary of the filtered simulation data
    """
    if not filtered_data:
        return f"No benchmarks found matching the {filter_description} criteria."

    summary_lines = [f"Summary of {filter_description} simulation benchmarks ({len(filtered_data)} total):"]
    summary_lines.append("=" * len(summary_lines[0]))

    # Calculate aggregate statistics
    kips_values = []
    success_rates = []
    total_qubits_list = []
    memory_swaps_list = []

    for benchmark_name, stats in filtered_data.items():
        if 'error' not in stats:
            if 'kips' in stats:
                kips_values.append(stats['kips'])
            if 'application_success_rate' in stats:
                success_rates.append(stats['application_success_rate'])
            if 'memory_swaps' in stats:
                memory_swaps_list.append(stats['memory_swaps'])

            # Calculate total qubits
            total_qubits = 0
            for key in ['compute_total_physical_qubits', 'factory_total_physical_qubits', 'memory_total_physical_qubits']:
                if key in stats:
                    total_qubits += stats[key]
            if total_qubits > 0:
                total_qubits_list.append(total_qubits)

    # Display individual benchmarks
    summary_lines.append("\nIndividual benchmarks:")
    for benchmark_name, stats in sorted(filtered_data.items()):
        if 'error' in stats:
            summary_lines.append(f"  {benchmark_name}: ERROR - {stats['error']}")
        else:
            kips = stats.get('kips', 'N/A')
            success_rate = stats.get('application_success_rate', 'N/A')
            memory_swaps = stats.get('memory_swaps', 'N/A')

            if isinstance(success_rate, (int, float)):
                success_rate = f"{success_rate*100:.1f}%"
            if isinstance(memory_swaps, int):
                memory_swaps = f"{memory_swaps:,}"

            summary_lines.append(f"  {benchmark_name}: {kips} KIPS, {success_rate} success, {memory_swaps} swaps")

    # Display aggregate statistics
    if kips_values:
        summary_lines.append(f"\nAggregate statistics:")
        summary_lines.append(f"  Average KIPS: {sum(kips_values)/len(kips_values):.2f}")
        summary_lines.append(f"  Min KIPS: {min(kips_values):.2f}")
        summary_lines.append(f"  Max KIPS: {max(kips_values):.2f}")

    if success_rates:
        avg_success = sum(success_rates) / len(success_rates)
        summary_lines.append(f"  Average success rate: {avg_success*100:.2f}%")
        summary_lines.append(f"  Min success rate: {min(success_rates)*100:.2f}%")
        summary_lines.append(f"  Max success rate: {max(success_rates)*100:.2f}%")

    if memory_swaps_list:
        summary_lines.append(f"  Average memory swaps: {sum(memory_swaps_list)//len(memory_swaps_list):,}")
        summary_lines.append(f"  Min memory swaps: {min(memory_swaps_list):,}")
        summary_lines.append(f"  Max memory swaps: {max(memory_swaps_list):,}")

    if total_qubits_list:
        summary_lines.append(f"  Average total qubits: {sum(total_qubits_list)//len(total_qubits_list):,}")
        summary_lines.append(f"  Min total qubits: {min(total_qubits_list):,}")
        summary_lines.append(f"  Max total qubits: {max(total_qubits_list):,}")

    return "\n".join(summary_lines)