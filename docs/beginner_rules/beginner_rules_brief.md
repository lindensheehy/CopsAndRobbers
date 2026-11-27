Out of scope:
- double move ticket
- black ticket

GAME LENGTH: 13 rounds (26 ply) - ROBBER goes first
robber ALWAYS starts on node 82

3 cop version: cops ALWAYS start on nodes 41, 46, and 124
4 cop version: cops ALWAYS start on nodes 41, 46, 124, and 142

no two cops may EVER share a node

the cops move one at a time, in any order

self edges are not included (no agent may ever stay in place)

the red edges are NOT included at all

"All playing pieces can only be moved to unoccupied stations" -> certain moves are illegal. for example, two cops may not swap positions with each other because the cops move one at a time.

blocked cops: if, on the cops' turn, there is NO order in which every cop can move to a station not occupied by another cop, the cops LOSE. (a cop moving onto the robber's station is always allowed - that's a capture.) in effect, the cops may never choose a move set that leaves them in such a position. this is not in the official rules; we added it because the official rules don't define what happens.

the robber BECOMES INVISIBLE on turns 3, 8, and 13. this means that: using move 3 as an example, the robber is VISIBLE after turn 2, but when turn 3 starts it becomes invisible. it remains invisible until after the cops move, at which point it makes another move before becoming visible again.

the cops CAN catch the robber while it is invisible.

the robber wins if all 26 ply get played without a capture