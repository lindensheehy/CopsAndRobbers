"""Independent bounded reference in a separate, time-limited process."""
import importlib.util,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('ref',ROOT/'experimental/oracle_cpu/test.py');refmod=importlib.util.module_from_spec(spec);spec.loader.exec_module(refmod)
a=json.loads(Path(sys.argv[1]).read_text());ref=refmod.Reference(a['rows'],a['k'])
solve,action=ref.game(a['horizon'],tuple(range(a['period'],a['horizon']+1,a['period'])))
c=tuple(a['start']);wins=[solve(c,(r,),1) for r in range(ref.n)]
# Return every winning first action, so benchmark can validate the chosen move.
moves={str(r):[list(d) for d in ref.moves[c] if action(c,(r,),1,d)] for r,w in enumerate(wins) if w and r not in c}
Path(sys.argv[2]).write_text(json.dumps({'wins':wins,'winning_actions':moves}))
