# bone_mapper.py - heuristic bone name mapping using fuzzy matching
import difflib
from typing import List, Dict

def score_name(a:str, b:str):
    a2 = a.replace('_',' ').lower()
    b2 = b.replace('_',' ').lower()
    return difflib.SequenceMatcher(None, a2, b2).ratio()

def map_bones(src_bones:List[str], dst_bones:List[str], min_score=0.5):
    mapping = {}
    for s in src_bones:
        best = None; best_score = 0.0
        for d in dst_bones:
            sc = score_name(s,d)
            if sc > best_score:
                best_score = sc; best = d
        if best_score >= min_score:
            mapping[s] = best
        else:
            # fallback heuristics: L/R swap
            swaps = [s.replace('L_','R_'), s.replace('R_','L_'), s.replace('_L','_R'), s.replace('_R','_L')]
            mapped = None
            for cand in swaps:
                if cand in dst_bones:
                    mapped = cand; break
            mapping[s] = mapped if mapped else best if best else dst_bones[0]
    return mapping

if __name__ == '__main__':
    src = ['L_arm','R_arm','spine_01','left_leg']
    dst = ['right_arm','left_arm','spine01','leg_L']
    print(map_bones(src,dst))
