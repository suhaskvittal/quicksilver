"""
Analysis tools for parsing compiler and simulator outputs and creating performance plots.

This module provides functions to:
1. Parse compiler output files and extract statistics
2. Parse simulator output files and extract nested statistics
3. Create barplots for relative performance comparison
"""

import re
import os
import json
import numpy as np
import matplotlib.pyplot as plt
from typing import Dict, List, Union, Optional, Tuple, Any
from scipy.stats import gmean

############################################
############################################

def pretty_print_dict(data: Dict[str, Any], indent: int = 2, max_width: int = 80) -> None:
    """
    Print a dictionary in a JSON-like format for better readability.

    Args:
        data: Dictionary to print
        indent: Number of spaces for each indentation level
        max_width: Maximum line width before wrapping

    Example:
        >>> stats = {'CLIENT_0': {'KIPS': 0.357558, 'INST_DONE': 1253388}}
        >>> pretty_print_dict(stats)
        {
          "CLIENT_0": {
            "KIPS": 0.357558,
            "INST_DONE": 1253388
          }
        }
    """
    def _format_value(value: Any, current_indent: int) -> str:
        """Format a value with proper indentation."""
        if isinstance(value, dict):
            if not value:
                return "{}"

            lines = ["{"]
            for i, (k, v) in enumerate(value.items()):
                comma = "," if i < len(value) - 1 else ""
                formatted_value = _format_value(v, current_indent + indent)
                lines.append(f"{' ' * (current_indent + indent)}\"{k}\": {formatted_value}{comma}")
            lines.append(f"{' ' * current_indent}}}")
            return "\n".join(lines)

        elif isinstance(value, (list, tuple)):
            if not value:
                return "[]"

            # For short lists, keep on one line
            if len(value) <= 3 and all(not isinstance(v, (dict, list, tuple)) for v in value):
                formatted_items = [json.dumps(v) if isinstance(v, str) else str(v) for v in value]
                one_line = f"[{', '.join(formatted_items)}]"
                if len(one_line) <= max_width - current_indent:
                    return one_line

            # Multi-line format for longer lists
            lines = ["["]
            for i, item in enumerate(value):
                comma = "," if i < len(value) - 1 else ""
                formatted_item = _format_value(item, current_indent + indent)
                lines.append(f"{' ' * (current_indent + indent)}{formatted_item}{comma}")
            lines.append(f"{' ' * current_indent}]")
            return "\n".join(lines)

        elif isinstance(value, str):
            return json.dumps(value)  # Properly escape strings

        elif isinstance(value, float):
            # Format floats with reasonable precision
            if abs(value) >= 1e6 or (abs(value) < 1e-3 and value != 0):
                return f"{value:.6e}"
            else:
                return f"{value:.6g}"

        else:
            return str(value)

    print(_format_value(data, 0))

############################################
############################################

def parse_compiler_output(file_path: str) -> Dict[str, Union[int, float]]:
    """
    Parse compiler output files and return a dictionary of statistics.
    
    Args:
        file_path: Path to the compiler output file (.out)
        
    Returns:
        Dictionary with statistic names as keys and their values as values
        
    Example:
        >>> stats = parse_compiler_output("out/qmem/compiled_results/dpt/cr2_120_c12.out")
        >>> print(stats['INST_DONE'])
        2472592
    """
    stats = {}
    
    try:
        with open(file_path, 'r') as f:
            lines = f.readlines()
        
        for line in lines:
            line = line.strip()
            # Skip progress lines and empty lines
            if line.startswith('[ MEMOPT ] progress:') or not line:
                continue
            
            # Parse statistic lines (format: STAT_NAME    VALUE)
            if re.match(r'^[A-Z_]+\s+\d+', line):
                parts = line.split()
                if len(parts) >= 2:
                    stat_name = parts[0]
                    try:
                        # Try to parse as int first, then float
                        value = int(parts[-1])
                    except ValueError:
                        try:
                            value = float(parts[-1])
                        except ValueError:
                            continue
                    stats[stat_name] = value
                    
    except FileNotFoundError:
        print(f"Warning: File {file_path} not found")
    except Exception as e:
        print(f"Error parsing {file_path}: {e}")
    
    return stats

############################################
############################################

def parse_simulator_output(file_path: str) -> Dict[str, Union[int, float, Dict]]:
    """
    Parse simulator output files and return a dictionary with potentially nested statistics.

    Args:
        file_path: Path to the simulator output file (.out)

    Returns:
        Dictionary with statistic names as keys. Values can be numbers or nested dictionaries
        for grouped statistics (e.g., CLIENT_0, FACTORY_L0, etc.)

    Example:
        >>> stats = parse_simulator_output("out/qmem/simulation_results/dpt/cr2_120_c12.out")
        >>> print(stats['CLIENT_0']['KIPS'])
        0.357558
    """
    stats = {}
    current_section = None

    try:
        with open(file_path, 'r') as f:
            lines = f.readlines()

        for line_raw in lines:
            line = line_raw.rstrip()  # Keep leading spaces but remove trailing whitespace

            # Skip empty lines and section headers
            if not line or line.startswith('---') or 'SIMULATION_STATS' in line:
                continue

            # Check for section headers (lines with no leading spaces, no colon, and uppercase)
            if not line.startswith(' ') and ':' not in line:
                # This is a section header
                current_section = line.strip()
                if current_section not in stats:
                    stats[current_section] = {}
                continue

            # Parse key-value pairs
            if ':' in line:
                if line.startswith(' ') and current_section:
                    # This is an indented line belonging to current section
                    clean_line = line.lstrip()
                    parts = clean_line.split(':', 1)

                    if len(parts) == 2:
                        key = parts[0].strip()
                        value_str = parts[1].strip()

                        # Try to parse the value
                        try:
                            # Handle scientific notation and regular numbers
                            if 'e' in value_str.lower() or ('.' in value_str and value_str.replace('.', '').replace('-', '').isdigit()):
                                value = float(value_str)
                            elif value_str.lstrip('-').isdigit():
                                value = int(value_str)
                            else:
                                value = value_str
                        except ValueError:
                            # Keep as string if can't parse as number
                            value = value_str

                        # Store in current section
                        stats[current_section][key] = value

                else:
                    # This is a top-level statistic (not indented)
                    clean_line = line.strip()
                    parts = clean_line.split(':', 1)

                    if len(parts) == 2:
                        key = parts[0].strip()
                        value_str = parts[1].strip()

                        # Try to parse the value
                        try:
                            if 'e' in value_str.lower() or ('.' in value_str and value_str.replace('.', '').replace('-', '').isdigit()):
                                value = float(value_str)
                            elif value_str.lstrip('-').isdigit():
                                value = int(value_str)
                            else:
                                value = value_str
                        except ValueError:
                            value = value_str

                        # Store as top-level statistic
                        stats[key] = value
                        # Reset current section since we hit a top-level stat
                        current_section = None

    except FileNotFoundError:
        print(f"Warning: File {file_path} not found")
    except Exception as e:
        print(f"Error parsing {file_path}: {e}")

    return stats

############################################
############################################

def create_performance_barplot(
    baseline_policy: str,
    policies: List[str],
    workloads: List[str],
    statistic: str = "KIPS",
    section: str = "CLIENT_0",
    data_dir: str = "out/qmem/simulation_results",
    is_simulation_stats: bool = True,
    figsize: Tuple[float, float] = (12, 6),
    colors: Optional[List[str]] = None,
    bar_width: float = 0.8,
    ylabel: str = "Relative Performance",
    ylabel_fontsize: int = 12,
    xlabel_fontsize: int = 12,
    show_values: bool = True,
    percent_improvement: bool = False,
    plotting_slowdown: bool = False,
    ext: str = None,
    legend_labels: Optional[List[str]] = None
) -> plt.Figure:
    """
    Create a barplot showing relative performance for any statistic from simulator outputs.

    Args:
        baseline_policy: Name of the baseline policy for normalization
        policies: List of policy names to compare
        workloads: List of workload names (will be x-axis labels)
        statistic: Name of the statistic to plot (e.g., "KIPS", "VIRTUAL_INST_DONE")
        section: Section name where the statistic is located (e.g., "CLIENT_0", "MEMORY", "FACTORY_L0")
        data_dir: Directory containing simulation results
        figsize: Figure size as (width, height)
        colors: List of colors for each policy. If None, uses default matplotlib colors
        bar_width: Width of each bar group
        ylabel: Y-axis label
        ylabel_fontsize: Font size for y-axis label
        xlabel_fontsize: Font size for x-axis label
        show_values: Whether to show values on top of bars
        percent_improvement: If True, show percentage improvement (relative - 1) instead of relative values
        legend_labels: Custom labels for legend. If None, uses policy names

    Returns:
        matplotlib Figure object

    Example:
        >>> # Plot KIPS performance
        >>> fig = create_performance_barplot(
        ...     baseline_policy="baseline",
        ...     policies=["baseline", "dpt", "viszlai"],
        ...     workloads=["cr2_120", "ethylene_240", "hc3h2cn_288", "shor_RSA256"],
        ...     statistic="KIPS",
        ...     section="CLIENT_0",
        ...     ylabel="Relative KIPS"
        ... )
        >>>
        >>> # Plot memory requests as percentage improvement
        >>> fig = create_performance_barplot(
        ...     baseline_policy="baseline",
        ...     policies=["baseline", "dpt", "viszlai"],
        ...     workloads=["cr2_120", "ethylene_240"],
        ...     statistic="ALL_REQUESTS",
        ...     section="MEMORY",
        ...     ylabel="Memory Requests Improvement (%)",
        ...     percent_improvement=True
        ... )
        >>> plt.show()
    """
    # Collect statistic data for all policies and workloads
    stat_data = {}

    policy_plus_baseline = [baseline_policy, *policies]
    for policy in policy_plus_baseline:
        stat_data[policy] = {}

        for workload in workloads:
            # Construct file path - baseline has different naming convention
            if ext is None or policy == 'baseline':
                file_path = os.path.join(data_dir, policy, f"{workload}.out")
            else:
                file_path = os.path.join(data_dir, policy, f"{workload}_{ext}.out")

            # Parse the file and extract the requested statistic
            if is_simulation_stats:
                stats = parse_simulator_output(file_path)
            else:
                stats = parse_compiler_output(file_path)

            # Handle both nested and top-level statistics
            if section and section in stats and isinstance(stats[section], dict) and statistic in stats[section]:
                stat_data[policy][workload] = stats[section][statistic]
            elif statistic in stats:
                # Top-level statistic
                stat_data[policy][workload] = stats[statistic]
            else:
                print(f"Warning: {statistic} not found in section '{section}' in {file_path}")
                stat_data[policy][workload] = 0
    
    # Calculate relative performance (normalized to baseline)
    relative_data = {}
    baseline_stats = stat_data.get(baseline_policy, {})

    for policy in policies:
        relative_data[policy] = []
        for workload in workloads:
            if workload in baseline_stats and baseline_stats[workload] > 0:
                relative_perf = stat_data[policy][workload] / baseline_stats[workload]
            else:
                relative_perf = 0
            if plotting_slowdown:
                relative_perf = 1.0/relative_perf
            relative_data[policy].append(relative_perf)
    
    # Convert to percentage improvement if requested
    if percent_improvement:
        for policy in policies:
            relative_data[policy] = [(x - 1) * 100 for x in relative_data[policy]]

    # Calculate geometric means
    geomeans = {}
    for policy in policies:
        if percent_improvement:
            # For percentage improvement, calculate geomean of (1 + improvement/100) then convert back
            values = [x for x in relative_data[policy] if x > -100]  # Avoid values that would make (1 + x/100) <= 0
            if values:
                # Convert back to relative values, calculate geomean, then convert to percentage
                relative_values = [(x / 100) + 1 for x in values]
                geomean_relative = gmean(relative_values)
                geomeans[policy] = (geomean_relative - 1) * 100
            else:
                geomeans[policy] = 0
        else:
            values = [x for x in relative_data[policy] if x > 0]
            if values:
                geomeans[policy] = gmean(values)
            else:
                geomeans[policy] = 0
    
    # Set up the plot
    fig, ax = plt.subplots()
    fig.set_size_inches(figsize[0], figsize[1])
    
    # Prepare data for plotting
    x_labels = workloads + ['Geomean']
    x_pos = np.arange(len(x_labels))
    print(x_labels)
    
    # Add gap before geomean
    x_pos[-1] = x_pos[-2] + 1.5
    
    # Set colors
    if colors is None:
        colors = plt.cm.Set1(np.linspace(0, 1, len(policies)))
    
    # Calculate bar positions
    bar_width_individual = bar_width / len(policies)
    
    for i, policy in enumerate(policies):
        # Prepare y values (workloads + geomean)
        y_values = relative_data[policy] + [geomeans[policy]]
        # Calculate x positions for this policy's bars
        x_positions = x_pos + (i - len(policies)/2 + 0.5) * bar_width_individual
        
        # Create bars
        label = legend_labels[i] if legend_labels and i < len(legend_labels) else policy
        bars = ax.bar(x_positions, y_values, bar_width_individual,
                     label=label, color=colors[i], alpha=0.8)
        
        # Add value labels on bars if requested
        if show_values:
            for bar, value in zip(bars, y_values):
                if percent_improvement:
                    # Format percentage values
                    if abs(value) >= 100:
                        label = f'{value:.0f}%'
                    else:
                        label = f'{value:.1f}%'
                else:
                    # Format relative values
                    label = f'{value:.2f}'

                # Position label above or below bar depending on value
                if value >= 0:
                    y_pos = bar.get_height() + (0.01 if not percent_improvement else 1)
                    va = 'bottom'
                else:
                    y_pos = bar.get_height() - (0.01 if not percent_improvement else 1)
                    va = 'top'

                ax.text(bar.get_x() + bar.get_width()/2, y_pos, label,
                       ha='center', va=va, fontsize=8)
    
    # Customize the plot\
    ax.set_ylabel(ylabel, fontsize=ylabel_fontsize)
    ax.set_xticks(x_pos, labels=x_labels, fontsize=xlabel_fontsize, rotation=20, ha='right')
    ax.legend(fontsize=10)

    # Add y-axis grid lines (light grey in background)
    ax.grid(True, axis='y', color='lightgrey', linestyle='-', alpha=0.7)
    ax.set_axisbelow(True)  # Ensure grid lines are behind the bars
    
    # Add a horizontal reference line
    if percent_improvement:
        # For percentage improvement, add line at 0% (no improvement)
        ax.axhline(y=0, color='black', linestyle='--', alpha=0.5)
    else:
        # For relative values, add line at 1 (baseline)
        ax.axhline(y=1, color='black', linestyle='--', alpha=0.5)
    
    # Add vertical line to separate geomean
    if len(workloads) > 0:
        ax.axvline(x=x_pos[-2] + 0.75, color='gray', linestyle='-', alpha=0.3)
    
    plt.tight_layout()
    return fig

############################################
############################################

def create_sensitivity_barplot(
    baseline_config: str,
    policy: str,
    configurations: List[str],
    workloads: List[str],
    statistic: str = "KIPS",
    section: str = "CLIENT_0",
    data_dir: str = "out/qmem/simulation_results",
    is_simulation_stats: bool = True,
    figsize: Tuple[float, float] = (12, 6),
    colors: Optional[List[str]] = None,
    bar_width: float = 0.8,
    ylabel: str = "Relative Performance",
    ylabel_fontsize: int = 12,
    xlabel_fontsize: int = 12,
    show_values: bool = True,
    percent_improvement: bool = False,
    plotting_slowdown: bool = False,
    ext: str = None,
    legend_labels: Optional[List[str]] = None
) -> plt.Figure:
    """
    Create a barplot for sensitivity studies comparing different configurations of a single policy.

    This function is designed for studying how different parameter values (e.g., c2, c4, c8, c12, c16, c24)
    affect performance for a single policy across multiple workloads.

    Args:
        baseline_config: Configuration to use as baseline for normalization (e.g., "c4")
        policy: Policy name (e.g., "dpt", "viszlai")
        configurations: List of configuration suffixes (e.g., ["c4", "c8", "c12", "c16", "c24"])
        workloads: List of workload names (will be x-axis labels)
        statistic: Name of the statistic to plot (e.g., "KIPS", "VIRTUAL_INST_DONE")
        section: Section name where the statistic is located (e.g., "CLIENT_0", "MEMORY", "FACTORY_L0")
        data_dir: Directory containing simulation results
        is_simulation_stats: If True, parse as simulation output; if False, parse as compiler output
        figsize: Figure size as (width, height)
        colors: List of colors for each configuration. If None, uses default matplotlib colors
        bar_width: Width of each bar group
        ylabel: Y-axis label
        ylabel_fontsize: Font size for y-axis label
        xlabel_fontsize: Font size for x-axis label
        show_values: Whether to show values on top of bars
        percent_improvement: If True, show percentage improvement (relative - 1) instead of relative values
        plotting_slowdown: If True, plot slowdown (1/relative_perf) instead of speedup
        ext: File extension/suffix to append to workload names (e.g., "c12" for workload_c12.out)
        legend_labels: Custom labels for legend. If None, uses configuration names

    Returns:
        matplotlib Figure object

    Example:
        >>> # Study how core count affects KIPS for DPT policy
        >>> fig = create_sensitivity_barplot(
        ...     baseline_config="c4",
        ...     policy="dpt",
        ...     configurations=["c4", "c8", "c12", "c16", "c24"],
        ...     workloads=["cr2_120", "ethylene_240", "hc3h2cn_288"],
        ...     statistic="KIPS",
        ...     section="CLIENT_0",
        ...     ylabel="Relative KIPS",
        ...     legend_labels=["4 cores", "8 cores", "12 cores", "16 cores", "24 cores"]
        ... )
        >>> plt.show()
    """
    # Collect statistic data for all configurations and workloads
    stat_data = {}

    for config in configurations:
        stat_data[config] = {}

        for workload in workloads:
            # Construct file path: policy/workload_config.out
            if ext is None:
                file_path = os.path.join(data_dir, policy, f"{workload}_{config}.out")
            else:
                file_path = os.path.join(data_dir, policy, f"{workload}_{ext}.out")

            # Parse the file and extract the requested statistic
            if is_simulation_stats:
                stats = parse_simulator_output(file_path)
            else:
                stats = parse_compiler_output(file_path)

            # Handle both nested and top-level statistics
            if section and section in stats and isinstance(stats[section], dict) and statistic in stats[section]:
                stat_data[config][workload] = stats[section][statistic]
            elif statistic in stats:
                # Top-level statistic
                stat_data[config][workload] = stats[statistic]
            else:
                print(f"Warning: {statistic} not found in section '{section}' in {file_path}")
                stat_data[config][workload] = 0


    # Calculate relative performance (normalized to baseline configuration)
    relative_data = {}
    baseline_stats = stat_data.get(baseline_config, {})

    for config in configurations:
        relative_data[config] = []
        for workload in workloads:
            if workload in baseline_stats and baseline_stats[workload] > 0:
                relative_perf = stat_data[config][workload] / baseline_stats[workload]
            else:
                relative_perf = 0
            if plotting_slowdown:
                relative_perf = 1.0/relative_perf
            relative_data[config].append(relative_perf)

    # Convert to percentage improvement if requested
    if percent_improvement:
        for config in configurations:
            relative_data[config] = [(x - 1) * 100 for x in relative_data[config]]

    # Calculate geometric means
    geomeans = {}
    for config in configurations:
        if percent_improvement:
            # For percentage improvement, calculate geomean of (1 + improvement/100) then convert back
            values = [x for x in relative_data[config] if x > -100]  # Avoid values that would make (1 + x/100) <= 0
            if values:
                # Convert back to relative values, calculate geomean, then convert to percentage
                relative_values = [(x / 100) + 1 for x in values]
                geomean_relative = gmean(relative_values)
                geomeans[config] = (geomean_relative - 1) * 100
            else:
                geomeans[config] = 0
        else:
            values = [x for x in relative_data[config] if x > 0]
            if values:
                geomeans[config] = gmean(values)
            else:
                geomeans[config] = 0

    # Set up the plot
    fig, ax = plt.subplots(figsize=figsize)

    # Prepare data for plotting
    x_labels = workloads + ['Geomean']
    x_pos = np.arange(len(x_labels))

    # Add gap before geomean
    x_pos[-1] = x_pos[-2] + 1.5

    # Set colors
    if colors is None:
        colors = plt.cm.Set1(np.linspace(0, 1, len(configurations)))

    # Calculate bar positions
    bar_width_individual = bar_width / len(configurations)

    for i, config in enumerate(configurations):
        # Prepare y values (workloads + geomean)
        y_values = relative_data[config] + [geomeans[config]]

        # Calculate x positions for this configuration's bars
        x_positions = x_pos + (i - len(configurations)/2 + 0.5) * bar_width_individual

        # Create bars
        label = legend_labels[i] if legend_labels and i < len(legend_labels) else config
        bars = ax.bar(x_positions, y_values, bar_width_individual,
                     label=label, color=colors[i], alpha=0.8)

        # Add value labels on bars if requested
        if show_values:
            for bar, value in zip(bars, y_values):
                if percent_improvement:
                    # Format percentage values
                    if abs(value) >= 100:
                        label_text = f'{value:.0f}%'
                    else:
                        label_text = f'{value:.1f}%'
                else:
                    # Format relative values
                    label_text = f'{value:.2f}'

                # Position label above or below bar depending on value
                if value >= 0:
                    y_pos = bar.get_height() + (0.01 if not percent_improvement else 1)
                    va = 'bottom'
                else:
                    y_pos = bar.get_height() - (0.01 if not percent_improvement else 1)
                    va = 'top'

                ax.text(bar.get_x() + bar.get_width()/2, y_pos, label_text,
                       ha='center', va=va, fontsize=8)

    # Customize the plot
    ax.set_xlabel('Workload', fontsize=xlabel_fontsize)
    ax.set_ylabel(ylabel, fontsize=ylabel_fontsize)
    ax.set_xticks(x_pos)
    ax.set_xticklabels(x_labels)
    ax.legend()

    # Add y-axis grid lines (light grey in background)
    ax.grid(True, axis='y', color='lightgrey', linestyle='-', alpha=0.7)
    ax.set_axisbelow(True)  # Ensure grid lines are behind the bars

    # Add a horizontal reference line
    if percent_improvement:
        # For percentage improvement, add line at 0% (no improvement)
        ax.axhline(y=0, color='black', linestyle='--', alpha=0.5)
    else:
        # For relative values, add line at 1 (baseline)
        ax.axhline(y=1, color='black', linestyle='--', alpha=0.5)

    # Add vertical line to separate geomean
    if len(workloads) > 0:
        ax.axvline(x=x_pos[-2] + 0.75, color='gray', linestyle='-', alpha=0.3)

    plt.tight_layout()
    return fig


def create_kips_barplot(
    baseline_policy: str,
    policies: List[str],
    workloads: List[str],
    data_dir: str = "out/qmem/simulation_results",
    is_simulation_stats: bool = True,
    figsize: Tuple[float, float] = (12, 6),
    colors: Optional[List[str]] = None,
    bar_width: float = 0.8,
    ylabel: str = "Relative KIPs",
    ylabel_fontsize: int = 12,
    xlabel_fontsize: int = 12,
    show_values: bool = True,
    percent_improvement: bool = False,
    plotting_slowdown: bool = False,
    ext: str = None,
    legend_labels: Optional[List[str]] = None
) -> plt.Figure:
    """
    Backward compatibility wrapper for create_performance_barplot with KIPS statistic.

    This function is deprecated. Use create_performance_barplot instead.
    """
    return create_performance_barplot(
        baseline_policy=baseline_policy,
        policies=policies,
        workloads=workloads,
        statistic="KIPS",
        section="CLIENT_0",
        data_dir=data_dir,
        is_simulation_stats=is_simulation_stats,
        figsize=figsize,
        colors=colors,
        bar_width=bar_width,
        ylabel=ylabel,
        ylabel_fontsize=ylabel_fontsize,
        xlabel_fontsize=xlabel_fontsize,
        show_values=show_values,
        percent_improvement=percent_improvement,
        plotting_slowdown=plotting_slowdown,
        ext=ext,
        legend_labels=legend_labels
    )


if __name__ == "__main__":
    # Example usage
    print("Testing compiler output parser:")
    compiler_stats = parse_compiler_output("out/qmem/compiled_results/dpt/cr2_120_c12.out")
    print("Sample compiler stats:")
    pretty_print_dict(compiler_stats)

    print("\nTesting simulator output parser:")
    sim_stats = parse_simulator_output("out/qmem/simulation_results/dpt/cr2_120_c12.out")
    print("Sample simulator stats (first few keys):")
    pretty_print_dict(sim_stats)

    if 'CLIENT_0' in sim_stats:
        print(f"\nCLIENT_0 section:")
        pretty_print_dict({'CLIENT_0': sim_stats['CLIENT_0']})

    print("\nTesting barplot creation (KIPS):")
    try:
        fig = create_performance_barplot(
            baseline_policy="baseline",
            policies=["baseline", "dpt", "viszlai"],
            workloads=["cr2_120", "ethylene_240", "hc3h2cn_288", "shor_RSA256"],
            statistic="KIPS",
            section="CLIENT_0",
            ylabel="Relative KIPS",
            ylabel_fontsize=14,
            xlabel_fontsize=12,
            figsize=(14, 6)
        )
        print("KIPS barplot created successfully!")
        # Uncomment the next line to display the plot
        plt.show()
    except Exception as e:
        print(f"Error creating KIPS barplot: {e}")

    print("\nTesting barplot creation (Memory Requests):")
    try:
        fig = create_performance_barplot(
            baseline_policy="baseline",
            policies=["baseline", "dpt", "viszlai"],
            workloads=["cr2_120", "ethylene_240"],
            statistic="ALL_REQUESTS",
            section="MEMORY",
            ylabel="Relative Memory Requests",
            ylabel_fontsize=14,
            xlabel_fontsize=12,
            figsize=(10, 6)
        )
        print("Memory requests barplot created successfully!")
        # Uncomment the next line to display the plot
        plt.show()
    except Exception as e:
        print(f"Error creating memory requests barplot: {e}")

    print("\nTesting KIPS barplot with percentage improvement:")
    try:
        fig = create_performance_barplot(
            baseline_policy="baseline",
            policies=["baseline", "dpt", "viszlai"],
            workloads=["cr2_120", "ethylene_240"],
            statistic="KIPS",
            section="CLIENT_0",
            ylabel="KIPS Improvement (%)",
            ylabel_fontsize=14,
            xlabel_fontsize=12,
            percent_improvement=True,
            figsize=(10, 6)
        )
        print("KIPS percentage improvement barplot created successfully!")
        # Uncomment the next line to display the plot
        # plt.show()
    except Exception as e:
        print(f"Error creating KIPS percentage improvement barplot: {e}")

    print("\nTesting sensitivity study barplot (DPT core count):")
    try:
        fig = create_sensitivity_barplot(
            baseline_config="c4",
            policy="dpt",
            configurations=["c4", "c8", "c12", "c16", "c24"],
            workloads=["cr2_120", "ethylene_240"],
            statistic="KIPS",
            section="CLIENT_0",
            ylabel="Relative KIPS",
            legend_labels=["4 cores", "8 cores", "12 cores", "16 cores", "24 cores"],
            figsize=(12, 6)
        )
        print("DPT sensitivity study barplot created successfully!")
        # Uncomment the next line to display the plot
        # plt.show()
    except Exception as e:
        print(f"Error creating DPT sensitivity study barplot: {e}")

    print("\nTesting sensitivity study with percentage improvement:")
    try:
        fig = create_sensitivity_barplot(
            baseline_config="c4",
            policy="viszlai",
            configurations=["c4", "c8", "c12"],
            workloads=["cr2_120", "ethylene_240"],
            statistic="KIPS",
            section="CLIENT_0",
            ylabel="KIPS Improvement (%)",
            percent_improvement=True,
            legend_labels=["4 cores", "8 cores", "12 cores"],
            figsize=(10, 6)
        )
        print("Viszlai sensitivity study (percentage) barplot created successfully!")
        # Uncomment the next line to display the plot
        # plt.show()
    except Exception as e:
        print(f"Error creating Viszlai sensitivity study barplot: {e}")

    print("\nTesting performance barplot with custom legend labels:")
    try:
        fig = create_performance_barplot(
            baseline_policy="baseline",
            policies=["baseline", "dpt", "viszlai"],
            workloads=["cr2_120", "ethylene_240"],
            statistic="KIPS",
            section="CLIENT_0",
            ylabel="Relative KIPS",
            legend_labels=["Baseline", "DPT Policy", "Viszlai Policy"],
            figsize=(10, 6)
        )
        print("Performance barplot with custom legend created successfully!")
        # Uncomment the next line to display the plot
        # plt.show()
    except Exception as e:
        print(f"Error creating performance barplot with custom legend: {e}")

    print("\nTesting sensitivity study with new parameters (slowdown mode):")
    try:
        fig = create_sensitivity_barplot(
            baseline_config="c4",
            policy="dpt",
            configurations=["c4", "c8", "c12"],
            workloads=["cr2_120", "ethylene_240"],
            statistic="KIPS",
            section="CLIENT_0",
            ylabel="Relative Slowdown",
            plotting_slowdown=True,  # NEW: Plot slowdown instead of speedup
            legend_labels=["4 cores", "8 cores", "12 cores"],
            figsize=(10, 6)
        )
        print("Sensitivity study with slowdown mode created successfully!")
        # Uncomment the next line to display the plot
        # plt.show()
    except Exception as e:
        print(f"Error creating sensitivity study with slowdown mode: {e}")

    print("\nTesting pretty_print_dict with sample data:")
    sample_data = {
        "config": {
            "policy": "dpt",
            "workload": "cr2_120",
            "cores": 12
        },
        "performance": {
            "KIPS": 0.357558,
            "cycles": 1.07485e+10,
            "instructions": [1253388, 10000047, 305641]
        },
        "success_rate": 0.870962
    }
    pretty_print_dict(sample_data)

