"""Build an offline HTML/Markdown report from one or many benchmark journals."""
import argparse, collections, csv, html, json, math, statistics, time
from pathlib import Path

def generate(root):
    root=Path(root); records=[]; damaged=[]
    for journal in sorted(root.rglob('runs.jsonl')):
        for i,line in enumerate(journal.read_text().splitlines()):
            try:
                row=json.loads(line);row['batch']=str(journal.parent.relative_to(root));records.append(row)
            except ValueError:damaged.append(f'{journal.name}:{i+1}')
    hard=[r for r in records if r.get('phase')=='hardened_fixed_start']
    old=[r for r in records if r.get('phase')=='v3_whole_table']
    oracle=[r for r in records if r.get('phase')=='oracle_build']
    def median(rs,key):
        vs=[r[key] for r in rs if isinstance(r.get(key),(int,float))]
        return f'{statistics.median(vs):.6f}' if vs else '—'
    sections=[]
    def section(title,text='',headers=None,rows=None):sections.append((title,text,headers,rows))
    section('Scope and interpretation', 'Original repository v3 performs a whole-table fixed-point search. Hardened performs fixed-start, finite-deadline searches with a reused full-visibility oracle. These are different workloads: no direct v3/hardened speedup is claimed. LOSS means no guaranteed capture within that deadline from that start, not infinite evasion. UNKNOWN and TIMEOUT are incomplete work. All reported times are measured on the computer running the suite.')
    section('Run totals',headers=['Phase','Runs','Outcomes','Median process seconds'],rows=[
        [name,len(rs),', '.join(f'{k}: {v}' for k,v in sorted(collections.Counter(r['status'] for r in rs).items())),median(rs,'wall_seconds')]
        for name,rs in [('V5 oracle generation',oracle),('Original v3 whole table',old),('Hardened fixed start/deadline',hard)]])
    pairs=collections.defaultdict(dict)
    for r in hard:
        pairs[(r['batch'],r['key'].rsplit('/',1)[0])][r['key'].rsplit('/',1)[-1]]=r
    disagreements=[];ratios=[];paired=0
    for key,vs in pairs.items():
        if 'cpu1' not in vs or 'cpu_all' not in vs:continue
        x,y=vs['cpu1'],vs['cpu_all'];paired+=1
        if {x['status'],y['status']}=={'WIN','LOSS'}:disagreements.append(str(key))
        if x['status']==y['status'] and x['status'] in ('WIN','LOSS') and min(x.get('search_seconds') or 0,y.get('search_seconds') or 0)>=.05:
            ratios.append(x['search_seconds']/y['search_seconds'])
    mismatch=sum(r.get('reference_mismatches',0) for r in hard);contradictions=sum(r.get('v3_bound_contradictions',0) for r in hard)
    section('Correctness evidence', 'Agreement is evidence, not a mathematical proof. The independent exhaustive Python check is attempted only for n ≤ 8 and ≤ 4,000 states. It checks per-robber answers and returned first actions. Larger cases rely on v3 positive-bound checks and aggregate thread-variant agreement; this report does not claim exhaustive larger-case verification.',
        ['Check','Count'],[['Independently checked hardened runs',sum(bool(r.get('reference_checked')) for r in hard)],['Independent reference mismatches',mismatch],['v3 positive-bound contradictions',contradictions],['Paired CPU thread runs',paired],['Opposite definite CPU verdicts',len(disagreements)],['Improved v3 recorded upper bounds (not errors)',sum(r.get('improved_v3_bound',0) for r in hard)],['v3-unmarked states newly won (review candidates)',sum(r.get('v3_unmarked_new_win',0) for r in hard)],['Malformed journal lines ignored',len(damaged)]])
    section('Multicore and oracle observations', 'CPU speedups below compare identical starts, deadlines and repeats, with matching definite verdicts. Only pairs taking at least 50 ms in both search variants are included. Process launch and oracle validation are excluded from this search comparison. Fixed execution order and cache effects remain limitations. Prune counts measure rejected candidates, not time saved; there is no oracle-off ablation in this suite.',
        ['Metric','Value'],[['Eligible thread-speedup pairs',len(ratios)],['Median CPU1 / multicore search time',f'{statistics.median(ratios):.3f}×' if ratios else 'Insufficient nontrivial completed pairs'],['Total recorded search nodes',sum(r.get('nodes') or 0 for r in hard)],['Total recorded pruned candidates',sum(r.get('pruned') or 0 for r in hard)],['GPU rows processed',sum(r.get('gpu_rows') or 0 for r in hard)]])
    groups=collections.defaultdict(list)
    for r in records:
        if r.get('phase') in ('v3_whole_table','hardened_fixed_start'):
            case=r['key'].split('/')[0];variant='v3' if r['phase']=='v3_whole_table' else r['key'].rsplit('/',1)[-1]
            groups[(r['batch'],case,r.get('period'),variant)].append(r)
    section('Measured results by graph, cop count and visibility', 'Search and wall medians include only completed definite results (or OK v3 completions). Outcomes retain all failures and censored runs. p means periodic 1/p visibility. Hardened groups combine the tested starts and deadlines; the full run ledger below separates them.',
        ['Batch / case','p','Solver','Runs','Outcomes','Median search s','Median wall s','Max nodes'],
        [[f'{b} / {c}',p,v,len(rs),dict(collections.Counter(r['status'] for r in rs)),median([r for r in rs if r['status'] in ('OK','WIN','LOSS')],'search_seconds'),median([r for r in rs if r['status'] in ('OK','WIN','LOSS')],'wall_seconds'),max((r.get('nodes') or 0 for r in rs),default=0)] for (b,c,p,v),rs in sorted(groups.items(),key=lambda x:str(x[0]))])
    inventory=root/'inventory.json'
    if inventory.exists():
        inv=json.loads(inventory.read_text());section('Repository coverage', 'Every repository matrix is inventoried. Scotland Yard variants are excluded from execution. Large combinations are resource-gated rather than allowed to consume the entire meeting preparation window. Pending cases have not been tested.', ['Graph','Vertices','Max degree','Cops','Planned action','Reason'],[[r.get(k,'') for k in ('graph','n','degree','k','action','reason')] for r in inv])
    N=199;K=4;target=N*math.comb(N+K-1,K)
    section('Scotland Yard: scale estimates, not a runtime promise',
        'For indistinguishable cops allowed to share vertices, configurations = C(n+k−1,k), and full-visibility (C,r) states = n × C(n+k−1,k). Scotland Yard with 199 vertices and four cops has 13,398,998,350 such states: a one-byte-per-state oracle needs 12.479 GiB plus its header. Partial visibility also depends on possible-robber sets, reveal phase, deadline, degrees and pruning effectiveness. Vertex count alone cannot give a trustworthy runtime upper bound. A loose count of possible information states is C(n+k−1,k) × 2^n × p, before time/deadline layers; not all of these are reachable. Increasing p can drastically change search, so no fitted p multiplier is asserted.')
    scenarios=[]
    for r in old:
        if r['status']!='OK' or not r.get('configs') or (r.get('search_seconds') or 0)<.05:continue
        source=r['configs']*r['vertices'];ratio=target/source;t=r['search_seconds']
        scenarios.append([r['batch']+'/'+r['key'],r.get('period'),source,f'{t:.3f}',f'{ratio:,.1f}',f'{t*ratio/3600:,.2f}',f'{t*ratio**2/3600:,.2f}'])
    section('Illustrative extrapolation scenarios', 'These are arithmetic scenarios for original v3 only: observed search time × state-count ratio, or × ratio². Neither is a bound, prediction or confidence interval; search branching and memory can invalidate both. They cannot estimate hardened time because its explored tree is not proportional to the full table. Baselines shorter than 50 ms are omitted. A defensible hardened estimate requires a budgeted Scotland Yard pilot measuring progress and search growth; until then its runtime is undetermined.',
        ['Measured baseline','p','Source states','Search s','State ratio','Linear scenario hours','Quadratic scenario hours'],rows=scenarios or [['No sufficiently long completed v3 baseline yet','—','—','—','—','—','—']])
    section('Reproducibility and limitations','The suite runs processes sequentially, never races them simultaneously. Each graph/cop combination builds an isolated V5 oracle first and reuses it across periods, horizons and CPU variants. Per-case settings.json records binary hashes; logs and raw JSONL retain failures. Oracle setup cost is reported separately. Transition gates are conservative estimates, not enforced RAM limits. GPU is off by default and no GPU performance claim follows from CPU tests. Coverage is bounded by the chosen time budget; a partial report is useful and explicitly incomplete. No new tokens or stay moves are added.')
    section('Complete run ledger',headers=['Batch','Run','Status','Wall s','Search s','Nodes','Prunes','Reference checked'],rows=[[r['batch'],r['key'],r['status'],r.get('wall_seconds',''),r.get('search_seconds',''),r.get('nodes',''),r.get('pruned',''),r.get('reference_checked','')] for r in records])
    stamp=time.strftime('%Y-%m-%d %H:%M:%S');markdown=['# Cops and Robbers — visibility benchmark',f'Generated {stamp}. Snapshot of {len(records)} recorded runs.']
    body=[f'<header><span>CAPSTONE • COMPUTATIONAL EXPERIMENTS</span><h1>Cops and Robbers</h1><p>Periodic visibility: original v3 and hardened oracle-guided search</p><small>Updated {stamp} · {len(records)} run records</small></header>']
    if mismatch or contradictions or disagreements:body.append('<p class="alert">Correctness discrepancies detected. Investigate before making solver-validity claims.</p>')
    for title,desc,heads,rows in sections:
        markdown.extend(['',f'## {title}',desc]);body.append('<section><h2>'+html.escape(title)+'</h2><p>'+html.escape(desc)+'</p>')
        if heads:
            markdown+=['',' | '.join(heads),' | '.join(['---']*len(heads))]
            body.append('<div class="table"><table><thead><tr>'+''.join('<th>'+html.escape(str(x))+'</th>' for x in heads)+'</tr></thead><tbody>')
            for row in rows:
                markdown.append(' | '.join(str(x).replace('|','/') for x in row));body.append('<tr>'+''.join('<td>'+html.escape(str(x))+'</td>' for x in row)+'</tr>')
            body.append('</tbody></table></div>')
        body.append('</section>')
    style='body{font:15px/1.55 system-ui,sans-serif;color:#203249;background:#edf2f7;margin:0}header{background:#112b46;color:white;padding:48px 6%}header span{color:#70ddd3;letter-spacing:2px;font-size:12px}h1{font-size:46px;margin:10px 0}section{background:white;margin:24px auto;padding:26px;max-width:1280px;border-radius:10px}h2{color:#143e60}p{max-width:1050px}.table{overflow:auto}table{border-collapse:collapse;width:100%;font-size:12px}th{text-align:left;background:#153c58;color:white}td,th{padding:9px;border-bottom:1px solid #dce5ed;vertical-align:top}tr:nth-child(even){background:#f3f7fa}.alert{background:#ffd9d9;padding:20px}@media print{body{background:white}section{margin:0;padding:12px;break-inside:auto}header{padding:20px}h1{font-size:30px}thead{display:table-header-group}tr{break-inside:avoid}.table{overflow:visible}td{overflow-wrap:anywhere}}'
    (root/'MEETING_REPORT.html').write_text('<!doctype html><html><head><meta charset="utf-8"><title>Visibility benchmark report</title><style>'+style+'</style></head><body>'+''.join(body)+'</body></html>',encoding='utf-8')
    (root/'MEETING_REPORT.md').write_text('\n'.join(markdown),encoding='utf-8')
    fields=sorted({k for r in records for k in r})
    with (root/'ALL_RUNS.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(records)
    return len(records)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('results',type=Path);a=p.parse_args();print('Records:',generate(a.results));print(a.results/'MEETING_REPORT.html')
