"""Build and run market-maker acceptance tests or the deterministic experiment."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent
SOURCES = [ROOT / 'FINALorderbook.cpp', *[ROOT / 'src' / name for name in
    ('quote_policy.cpp', 'simulation_account.cpp', 'engine_adapter.cpp', 'market_maker.cpp', 'scenario.cpp')]]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('test', 'demo'))
    parser.add_argument('--scenario', type=Path, default=ROOT / 'data' / 'market_maker_scenario.csv')
    parser.add_argument('--output', type=Path, default=ROOT / 'build' / 'experiment')
    args = parser.parse_args()
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('A C++20 compiler is required.')
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix='market_maker_build_') as directory:
        build = Path(directory)
        flags = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-pedantic', '-I', str(ROOT / 'include')]
        objects = []
        for source in SOURCES:
            obj = build / (source.stem + '.o')
            subprocess.run([*flags, '-DORDERBOOK_LIBRARY_ONLY', '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        def executable(source, name):
            target = build / (name + ('.exe' if __import__('os').name == 'nt' else ''))
            subprocess.run([*flags, str(source), *objects, '-o', str(target)], check=True)
            return target
        demo = executable(ROOT / 'src' / 'market_maker_demo.cpp', 'demo')
        if args.command == 'demo':
            if any(args.output.glob('*_trades.csv')):
                raise SystemExit('Choose a fresh --output directory; raw trade files append and must not mix runs.')
            subprocess.run([str(demo), str(args.scenario), str(args.output)], check=True)
        else:
            regression = build / 'regression'
            subprocess.run([*flags, str(ROOT / 'FINALorderbook.cpp'), '-o', str(regression)], check=True)
            subprocess.run([str(regression)], check=True, cwd=build)
            print('PASS existing orderbook and candle regressions', flush=True)
            for name in ('quote_policy_test', 'simulation_account_test', 'market_maker_integration_test', 'market_order_test'):
                subprocess.run([str(executable(ROOT / 'tests' / (name + '.cpp'), name))], check=True, cwd=build)
            first, second = build / 'first', build / 'second'
            for output in (first, second):
                subprocess.run([str(demo), str(args.scenario), str(output)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            assert {p.name: p.read_bytes() for p in first.iterdir()} == {p.name: p.read_bytes() for p in second.iterdir()}, 'Scenario outputs differ between identical runs.'
            print('PASS saved scenario determinism for 100/300/500 bps and 0/2 bps skew: executions, candles, accounts, metrics', flush=True)
            subprocess.run([sys.executable, str(ROOT / 'tests' / 'web_market_maker_test.py')], check=True, cwd=build)
    print(f'Build and {args.command} completed in {time.perf_counter() - started:.2f}s.')

if __name__ == '__main__':
    main()
