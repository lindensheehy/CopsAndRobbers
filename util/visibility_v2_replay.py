"""Replay the v2 reveal-boundary cache; Python 3, standard library only.

Run from the repository: python util/visibility_v2_replay.py
No solver is launched and no cache is modified. Cache labels are checked along
the replay, not independently proven globally optimal. Both players optimize
against those labels. Cops never consult the actual hidden robber position.
"""
import argparse
import itertools
import math
from pathlib import Path
import queue
import struct
import threading
import tkinter as tk
from tkinter import ttk, messagebox

ROOT = Path(__file__).resolve().parents[1]
CACHE = "k_cops_v2_2vis__scotlandyard-all__2-cops__2-vis__00__auxgraph.bin"
INF = 10**9


def vertices(mask):
    while mask:
        bit = mask & -mask
        yield bit.bit_length() - 1
        mask ^= bit


class Game:
    def __init__(self, cache, matrix, positions, p):
        self.p = p
        rows = []
        for line in matrix.read_text().splitlines():
            line = line.strip()
            if line.startswith('-'):
                break
            if line:
                if set(line) - {'0', '1'}:
                    raise ValueError('Matrix must contain binary rows.')
                rows.append(line)
        self.n = n = len(rows)
        if not n or any(len(row) != n for row in rows):
            raise ValueError('Matrix is not square.')
        if any(rows[i][j] != rows[j][i] for i in range(n) for j in range(n)):
            raise ValueError('This replay requires an undirected graph.')
        if any(rows[i][i] != '0' for i in range(n)):
            raise ValueError('Expected the no-stay graph used by v2.')
        self.adj = [tuple(j for j, x in enumerate(row) if x == '1') for row in rows]
        if any(not a for a in self.adj):
            raise ValueError('Isolated vertices are not supported by this replay.')
        self.neighbors = [sum(1 << j for j in a) for a in self.adj]
        self.coords = []
        for line in positions.read_text().splitlines():
            if line.strip() and not line.startswith('-'):
                x, y = map(float, line.split(',')[:2])
                if not math.isfinite(x) or not math.isfinite(y):
                    raise ValueError('Non-finite board coordinate.')
                self.coords.append((x, y))
        if len(self.coords) != n:
            raise ValueError('Position count does not match the matrix.')
        blob = cache.read_bytes()
        if len(blob) < 96:
            raise ValueError('Truncated cache header.')
        k, cn, cols, count, states, stride, cs, rs, a, b, c, d = struct.unpack_from('<12Q', blob)
        if (cn, cols, stride, cs, rs) != (n, 1, 1, 0, 0):
            raise ValueError('Expected a v2 one-plane, one-byte, no-stay cache.')
        if not 1 <= k <= 4 or count != math.comb(n + k - 1, k):
            raise ValueError('Invalid cop configuration count.')
        if not (a == 96 and b-a == count*k and c-b == (count+1)*8
                and c <= d <= len(blob) and (d-c) % 8 == 0
                and states == count*n and len(blob)-d == states):
            raise ValueError('Invalid cache section lengths.')
        self.k = k
        self.configs = [tuple(blob[i:i+k]) for i in range(a, b, k)]
        self.index = {v: i for i, v in enumerate(self.configs)}
        if len(self.index) != count or any(tuple(sorted(v)) != v or max(v) >= n for v in self.configs):
            raise ValueError('Invalid cached cop positions.')
        self.heads = struct.unpack_from(f'<{count+1}Q', blob, b)
        self.transitions = struct.unpack_from(f'<{(d-c)//8}Q', blob, c)
        if (self.heads[0] != 0 or self.heads[-1] != len(self.transitions)
                or any(x > y for x, y in zip(self.heads, self.heads[1:]))):
            raise ValueError('Invalid transition offsets.')
        if any(t % n or t >= states for t in self.transitions):
            raise ValueError('Invalid transition state index.')
        self.data = blob[d:]
        self.occupied = [sum(1 << x for x in set(v)) for v in self.configs]
        self.checked_moves = {}

    def value(self, c, r):
        b = self.data[c*self.n+r]
        return b >> 1 if b & 1 else INF

    def moves(self, c):
        if c not in self.checked_moves:
            expected = {self.index[tuple(sorted(v))] for v in
                        itertools.product(*(self.adj[x] for x in self.configs[c]))}
            stored = {t // self.n for t in self.transitions[self.heads[c]:self.heads[c+1]]}
            if expected != stored:
                raise ValueError('Cache transitions disagree with the matrix. Use matching files.')
            self.checked_moves[c] = tuple(sorted(expected))
        return self.checked_moves[c]

    def expand(self, mask):
        out = 0
        for r in vertices(mask):
            out |= self.neighbors[r]
        return out

    def plan(self, c, r):
        """Minimize over shared cop plans, maximize over the entire cloud."""
        best, chosen = INF, None

        def visit(prev, cloud, depth, path):
            nonlocal best, chosen
            if 2*depth-1 >= best:
                return
            for nxt in self.moves(prev):
                route = path + (nxt,)
                mid = cloud & ~self.occupied[nxt]
                after = self.expand(mid) & ~self.occupied[nxt]
                if not mid:
                    cost = 2*depth-1
                elif not after:
                    cost = 2*depth
                elif depth == self.p:
                    cost = 2*self.p + max(self.value(nxt, q) for q in vertices(after))
                else:
                    visit(nxt, after, depth+1, route)
                    continue
                if cost < best:
                    best, chosen = cost, route
        visit(c, 1 << r, 1, ())
        stored = self.value(c, r)
        if best != stored or chosen is None:
            raise ValueError(f'Cache/minimax mismatch at {self.configs[c]}, robber {r}: '
                             f'stored {stored}, reconstructed {best}. Replay stopped.')
        return best, chosen

    def robber_response(self, route, r):
        """Worst legal path against a fixed public cop plan; ties use vertex ID."""
        memo = {}

        def visit(i, q):
            key = (i, q)
            if key in memo:
                return memo[key]
            c = route[i]
            if q in self.configs[c]:
                answer = (1, ())
            else:
                best, path = -1, ()
                for v in self.adj[q]:
                    if v in self.configs[c]:
                        score, tail = 2, ()
                    elif i+1 == len(route):
                        if len(route) != self.p:
                            raise ValueError('Robber survived a claimed early capture.')
                        score, tail = 2+self.value(c, v), ()
                    else:
                        future, tail = visit(i+1, v)
                        score = 2+future
                    if score > best:
                        best, path = score, (v,)+tail
                answer = best, path
            memo[key] = answer
            return answer
        return visit(0, r)

    def history(self, status):
        best, start = INF, None
        for c in range(len(self.configs)):
            row = self.data[c*self.n:(c+1)*self.n]
            if all(b & 1 for b in row):
                worst = max(b >> 1 for b in row)
                if worst < best:
                    best, start = worst, c
        if start is None:
            raise ValueError('Cache contains no universally winning start.')
        c = start
        r = max(range(self.n), key=lambda q: self.value(c, q))
        cloud, visible, ply, remaining = 1 << r, True, 0, best
        frames = []

        def append(event, caught=False):
            if not caught and not cloud & (1 << r):
                raise ValueError('Actual robber escaped the information cloud.')
            frames.append(dict(cops=self.configs[c], robber=r, cloud=tuple(vertices(cloud)),
                               visible=visible or caught, ply=ply, event=event,
                               remaining=remaining, caught=caught))
        append('Initial reveal')
        while r not in self.configs[c]:
            status(f'Reconstructing at ply {ply}: {remaining} plies remain…')
            bound, route = self.plan(c, r)
            response, path = self.robber_response(route, r)
            if response != bound or bound != remaining:
                raise ValueError('Robber response disagrees with the minimax value.')
            for i, nxt in enumerate(route):
                c = nxt
                ply += 1
                remaining -= 1
                caught = r in self.configs[c]
                cloud &= ~self.occupied[c]
                append('Cops move' if not caught else 'Captured by cops', caught)
                if caught:
                    break
                old = r
                r = path[i]
                if r not in self.adj[old]:
                    raise ValueError('Illegal robber move.')
                ply += 1
                remaining -= 1
                caught = r in self.configs[c]
                cloud = self.expand(cloud) & ~self.occupied[c]
                visible = (i+1 == self.p)
                if visible:
                    cloud = 1 << r
                append('Robber captured' if caught else
                       ('Robber moves · REVEALED' if visible else 'Robber moves · HIDDEN'), caught)
                if caught:
                    break
            if frames[-1]['caught']:
                break
            if not visible or self.value(c, r) != remaining:
                raise ValueError('Reveal-boundary value did not decrease consistently.')
            if ply > best:
                raise ValueError('Replay exceeded its capture bound.')
        if ply != best or remaining != 0:
            raise ValueError('Final replay length disagrees with the selected worst case.')
        status(f'Checked replay: {ply} plies; capture on cop-team move {(ply+1)//2}.')
        return frames


class Viewer:
    def __init__(self, root, args):
        self.root, self.args = root, args
        self.frames, self.game, self.step = [], None, 0
        self.playing = False
        self.timer = None
        self.events = queue.Queue()
        root.title('Scotland Yard · Partial visibility replay')
        root.geometry('1380x960')
        root.minsize(900, 660)
        root.configure(bg='#f5f7fb')
        self.title = tk.StringVar(value='Loading v2 cache…')
        self.detail = tk.StringVar(value='No solver is being run. Reconstructing from saved values.')
        tk.Label(root, textvariable=self.title, font=('Segoe UI', 18, 'bold'),
                 bg='#f5f7fb', fg='#17243c', anchor='w').pack(fill='x', padx=24, pady=(16, 3))
        tk.Label(root, textvariable=self.detail, font=('Segoe UI', 10),
                 bg='#f5f7fb', fg='#52627b', anchor='w').pack(fill='x', padx=24)
        self.canvas = tk.Canvas(root, bg='#f5f7fb', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True, padx=14, pady=8)
        bar = ttk.Frame(root, padding=(20, 6))
        bar.pack(fill='x')
        for text, command in [('Restart', lambda: self.seek(0)), ('◀ Previous', lambda: self.seek(self.step-1)),
                              ('Play / Pause', self.toggle), ('Next ▶', lambda: self.seek(self.step+1))]:
            ttk.Button(bar, text=text, command=command).pack(side='left', padx=3)
        self.show_hidden = tk.BooleanVar(value=False)
        self.labels = tk.BooleanVar(value=True)
        ttk.Checkbutton(bar, text='Show hidden robber (viewer only)', variable=self.show_hidden,
                        command=self.draw).pack(side='left', padx=12)
        ttk.Checkbutton(bar, text='Vertex IDs', variable=self.labels, command=self.draw).pack(side='left')
        self.speed = tk.DoubleVar(value=900)
        ttk.Label(bar, text='Delay (ms)').pack(side='left', padx=(12, 4))
        ttk.Scale(bar, from_=200, to=2000, variable=self.speed, length=110).pack(side='left')
        self.scale = tk.Scale(root, from_=0, to=1, orient='horizontal', showvalue=False,
                              command=self.scrub, bg='#f5f7fb', highlightthickness=0)
        self.scale.pack(fill='x', padx=26)
        tk.Label(root, text='BLUE  Cops     RED  Revealed robber     YELLOW  Possible robber locations'
                 '     ·     IDs match the solver (0-based)', bg='#f5f7fb', fg='#52627b',
                 font=('Segoe UI', 10)).pack(pady=(2, 10))
        self.canvas.bind('<Configure>', lambda _: self.draw())
        root.bind('<Left>', lambda _: self.seek(self.step-1))
        root.bind('<Right>', lambda _: self.seek(self.step+1))
        root.bind('<space>', lambda _: self.toggle())
        threading.Thread(target=self.load, daemon=True).start()
        root.after(100, self.poll)

    def load(self):
        try:
            game = Game(self.args.cache, self.args.matrix, self.args.positions, self.args.p)
            frames = game.history(lambda text: self.events.put(('status', text)))
            self.events.put(('ready', (game, frames)))
        except Exception as exc:
            self.events.put(('error', str(exc)))

    def poll(self):
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == 'status':
                    self.detail.set(value)
                    print(value, flush=True)
                elif kind == 'error':
                    self.title.set('Replay stopped: validation failed')
                    self.detail.set(value)
                    messagebox.showerror('Cannot reconstruct replay', value)
                    return
                elif kind == 'ready':
                    self.game, self.frames = value
                    self.scale.configure(to=len(self.frames)-1)
                    self.draw()
                    return
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def scrub(self, value):
        if self.frames:
            self.step = min(len(self.frames)-1, max(0, int(float(value))))
            self.draw()

    def seek(self, step):
        if self.frames:
            self.playing = False
            if self.timer is not None:
                self.root.after_cancel(self.timer)
                self.timer = None
            self.step = max(0, min(step, len(self.frames)-1))
            self.scale.set(self.step)
            self.draw()

    def toggle(self):
        if not self.frames:
            return
        self.playing = not self.playing
        if self.timer is not None:
            self.root.after_cancel(self.timer)
            self.timer = None
        if self.playing:
            self.timer = self.root.after(int(self.speed.get()), self.tick)

    def tick(self):
        self.timer = None
        if not self.playing:
            return
        if self.step == len(self.frames)-1:
            self.playing = False
            return
        self.step += 1
        self.scale.set(self.step)
        self.draw()
        self.timer = self.root.after(int(self.speed.get()), self.tick)

    def draw(self):
        if not self.frames:
            return
        f, g, cv = self.frames[self.step], self.game, self.canvas
        cv.delete('all')
        w, h = max(cv.winfo_width(), 100), max(cv.winfo_height(), 100)
        xs, ys = zip(*g.coords)
        ratio = min((w-70)/max(max(xs)-min(xs), 1), (h-50)/max(max(ys)-min(ys), 1))
        ox = (w-(max(xs)-min(xs))*ratio)/2
        oy = (h-(max(ys)-min(ys))*ratio)/2
        points = [(ox+(x-min(xs))*ratio, oy+(y-min(ys))*ratio) for x, y in g.coords]
        for u, neighbors in enumerate(g.adj):
            for v in neighbors:
                if u < v:
                    cv.create_line(*points[u], *points[v], fill='#c3ccd9', width=1)

        def circle(v, radius, fill, outline='', width=1):
            x, y = points[v]
            return cv.create_oval(x-radius, y-radius, x+radius, y+radius,
                                  fill=fill, outline=outline, width=width)
        # Separate halos preserve the exact discrete set; no convex hull suggests
        # that unrelated vertices between possible positions are also feasible.
        if not f['visible']:
            for v in f['cloud']:
                circle(v, 19, '#fff0a6')
                circle(v, 12, '#ffd65b')
        for v in range(g.n):
            circle(v, 5, '#ffffff', '#94a4ba')
            if self.labels.get():
                x, y = points[v]
                cv.create_text(x, y-11, text=str(v), fill='#687890', font=('Segoe UI', 7))
        for v in sorted(set(f['cops'])):
            circle(v, 13, '#2878df', '#ffffff', 2)
            x, y = points[v]
            count = f['cops'].count(v)
            cv.create_text(x, y, text='C' if count == 1 else str(count),
                           fill='white', font=('Segoe UI', 9, 'bold'))
        if f['visible'] or self.show_hidden.get():
            v = f['robber']
            if f['caught']:
                circle(v, 19, '', '#e04848', 4)
            else:
                circle(v, 11, '#e04848' if f['visible'] else '#ffffff', '#e04848', 2)
                x, y = points[v]
                cv.create_text(x, y, text='R' if f['visible'] else '?',
                               fill='white' if f['visible'] else '#e04848', font=('Segoe UI', 9, 'bold'))
        round_text = 'Setup' if f['ply'] == 0 else f"Round {(f['ply']+1)//2}"
        self.title.set(f"{round_text}  ·  Ply {f['ply']} / {len(self.frames)-1}  ·  {f['event']}")
        robber = str(f['robber']) if f['visible'] else 'hidden'
        self.detail.set(f"Cops {f['cops']}   |   Robber: {robber}   |   "
                        f"Possible positions: {len(f['cloud']) if not f['caught'] else 0}   |   "
                        f"Replay remaining: {f['remaining']} plies   |   Reveals every {g.p} robber moves")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, default=ROOT/'cache'/CACHE)
    parser.add_argument('--matrix', type=Path, default=ROOT/'assets/matrices/scotlandyard-all.txt')
    parser.add_argument('--positions', type=Path, default=ROOT/'assets/positions/scotlandyard.txt')
    parser.add_argument('--p', type=int, default=2, help='Must match the cache; header does not store p.')
    args = parser.parse_args()
    if not 1 <= args.p <= 3:
        parser.error('This interactive replay supports p=1..3.')
    for path in (args.cache, args.matrix, args.positions):
        if not path.is_file():
            parser.error(f'File not found: {path}')
    expected = f'__{args.p}-vis__'
    if expected not in args.cache.name:
        parser.error(f'Cache filename must contain {expected} to identify its visibility interval.')
    root = tk.Tk()
    Viewer(root, args)
    root.mainloop()


if __name__ == '__main__':
    main()
