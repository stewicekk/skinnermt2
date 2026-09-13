# bone_mapper_train.py - create mapping rules from examples
# Usage:
# 1) Prepare a folder 'examples/' with JSON files containing {'src': 'L_arm', 'dst': 'right_arm'} entries
# 2) Run: python bone_mapper_train.py --in examples --out bone_map_rules.json

import os, json, argparse, difflib
from pathlib import Path

def score_name(a,b):
    a2 = a.replace('_',' ').lower()
    b2 = b.replace('_',' ').lower()
    return difflib.SequenceMatcher(None,a2,b2).ratio()

def aggregate_examples(folder):
    mapping_counts = {}
    for p in Path(folder).glob('*.json'):
        j = json.loads(p.read_text())
        s = j.get('src'); d = j.get('dst')
        if not s or not d: continue
        mapping_counts.setdefault(s, {})
        mapping_counts[s][d] = mapping_counts[s].get(d,0) + 1
    # choose top dst per src
    rules = {}
    for s, candidates in mapping_counts.items():
        best = max(candidates.items(), key=lambda x: x[1])[0]
        rules[s] = best
    return rules

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--in', dest='inf', required=True)
    parser.add_argument('--out', default='bone_map_rules.json')
    args = parser.parse_args()
    rules = aggregate_examples(args.inf)
    Path(args.out).write_text(json.dumps(rules, indent=2))
    print('Wrote', args.out)
