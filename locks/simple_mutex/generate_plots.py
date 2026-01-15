#!/usr/bin/env python3
"""
generate_plots.py - Generate performance plots from benchmark results
"""

import matplotlib.pyplot as plt
import numpy as np
import re

def parse_throughput_results(filename):
    """Parse throughput curve results from benchmark output"""
    threads = []
    simple_throughput = []
    pthread_throughput = []
    
    with open(filename, 'r') as f:
        lines = f.readlines()
    
    # Look for throughput table
    in_table = False
    for line in lines:
        if 'Threads | simple_mutex_t' in line:
            in_table = True
            continue
        if in_table and line.strip() and '--------' not in line:
            parts = line.split('|')
            if len(parts) >= 3:
                try:
                    t = int(parts[0].strip())
                    s = float(parts[1].strip().split()[0])
                    p = float(parts[2].strip().split()[0])
                    threads.append(t)
                    simple_throughput.append(s)
                    pthread_throughput.append(p)
                except:
                    continue
    
    return threads, simple_throughput, pthread_throughput

def parse_fairness_results(filename):
    """Parse fairness results from benchmark output"""
    fairness_data = {}
    
    with open(filename, 'r') as f:
        content = f.read()
    
    # Extract simple_mutex fairness ratio
    simple_match = re.search(r'simple_mutex_t.*?Fairness ratio.*?(\d+\.\d+)', content, re.DOTALL)
    pthread_match = re.search(r'Pthread fairness ratio.*?(\d+\.\d+)', content)
    
    if simple_match:
        fairness_data['simple'] = float(simple_match.group(1))
    if pthread_match:
        fairness_data['pthread'] = float(pthread_match.group(1))
    
    return fairness_data

def create_throughput_plot(threads, simple_throughput, pthread_throughput, output_file='throughput.png'):
    """Create throughput vs threads plot"""
    plt.figure(figsize=(10, 6))
    
    plt.plot(threads, simple_throughput, 'o-', label='simple_mutex_t', linewidth=2, markersize=8)
    plt.plot(threads, pthread_throughput, 's-', label='pthread_mutex_t', linewidth=2, markersize=8)
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Throughput (M ops/sec)', fontsize=12)
    plt.title('Mutex Throughput vs Contention', fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.legend(fontsize=11)
    plt.tight_layout()
    
    plt.savefig(output_file, dpi=150)
    print(f"Saved throughput plot to {output_file}")

def create_scalability_plot(threads, simple_throughput, pthread_throughput, output_file='scalability.png'):
    """Create normalized scalability plot"""
    if not simple_throughput or not pthread_throughput:
        return
    
    # Normalize to single-thread performance
    simple_norm = [t/simple_throughput[0] for t in simple_throughput]
    pthread_norm = [t/pthread_throughput[0] for t in pthread_throughput]
    
    plt.figure(figsize=(10, 6))
    
    plt.plot(threads, simple_norm, 'o-', label='simple_mutex_t', linewidth=2, markersize=8)
    plt.plot(threads, pthread_norm, 's-', label='pthread_mutex_t', linewidth=2, markersize=8)
    plt.plot(threads, [1.0]*len(threads), 'k--', label='Ideal', alpha=0.5)
    
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Normalized Throughput (vs 1 thread)', fontsize=12)
    plt.title('Mutex Scalability', fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    plt.legend(fontsize=11)
    plt.tight_layout()
    
    plt.savefig(output_file, dpi=150)
    print(f"Saved scalability plot to {output_file}")

def create_fairness_chart(fairness_data, output_file='fairness.png'):
    """Create fairness comparison chart"""
    if not fairness_data:
        return
    
    labels = list(fairness_data.keys())
    values = list(fairness_data.values())
    
    plt.figure(figsize=(8, 6))
    bars = plt.bar(labels, values, color=['#ff6b6b', '#4ecdc4'])
    
    plt.ylabel('Fairness Ratio (min/max)', fontsize=12)
    plt.title('Mutex Fairness Comparison', fontsize=14, fontweight='bold')
    plt.ylim(0, 1.1)
    plt.grid(True, axis='y', alpha=0.3)
    
    # Add value labels on bars
    for bar, val in zip(bars, values):
        plt.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.01,
                f'{val:.3f}', ha='center', va='bottom', fontweight='bold')
    
    plt.tight_layout()
    plt.savefig(output_file, dpi=150)
    print(f"Saved fairness chart to {output_file}")

def create_cs_sensitivity_plot(filename, output_file='cs_sensitivity.png'):
    """Create critical section sensitivity plot"""
    cs_sizes = []
    simple_throughput = []
    pthread_throughput = []
    
    with open(filename, 'r') as f:
        lines = f.readlines()
    
    # Parse CS sensitivity table
    for line in lines:
        if 'CS Size (ns) |' in line:
            continue
        if '|' in line and 'simple_mutex_t' not in line:
            parts = line.split('|')
            if len(parts) >= 3:
                try:
                    cs = int(parts[0].strip())
                    s = float(parts[1].strip().split()[0])
                    p = float(parts[2].strip().split()[0])
                    cs_sizes.append(cs)
                    simple_throughput.append(s)
                    pthread_throughput.append(p)
                except:
                    continue
    
    if not cs_sizes:
        return
    
    plt.figure(figsize=(10, 6))
    
    # Use log scale for x-axis (CS size)
    plt.semilogx(cs_sizes, simple_throughput, 'o-', label='simple_mutex_t', linewidth=2, markersize=8)
    plt.semilogx(cs_sizes, pthread_throughput, 's-', label='pthread_mutex_t', linewidth=2, markersize=8)
    
    plt.xlabel('Critical Section Size (ns, log scale)', fontsize=12)
    plt.ylabel('Throughput (K ops/sec)', fontsize=12)
    plt.title('Performance vs Critical Section Size', fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3, which='both')
    plt.legend(fontsize=11)
    plt.tight_layout()
    
    plt.savefig(output_file, dpi=150)
    print(f"Saved CS sensitivity plot to {output_file}")

def main():
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python3 generate_plots.py <benchmark_results.txt>")
        sys.exit(1)
    
    results_file = sys.argv[1]
    
    print(f"Parsing results from {results_file}...")
    
    # Parse and create plots
    threads, simple_throughput, pthread_throughput = parse_throughput_results(results_file)
    fairness_data = parse_fairness_results(results_file)
    
    if threads and simple_throughput and pthread_throughput:
        create_throughput_plot(threads, simple_throughput, pthread_throughput)
        create_scalability_plot(threads, simple_throughput, pthread_throughput)
    
    if fairness_data:
        create_fairness_chart(fairness_data)
    
    create_cs_sensitivity_plot(results_file)
    
    print("\nAll plots generated successfully!")

if __name__ == "__main__":
    main()