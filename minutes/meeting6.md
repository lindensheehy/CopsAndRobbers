4 columns as discussed
- from 1 -> 2: same rule as initial algo (there exists)
- from 2 -> 3: mapping is 1 to 1, so using the for all rule, we can mark nodes in column 2 as cooresponding nodes in column 3 get marked
- from 3 -> 4: simple there exists
- from 4 -> 1: for all robber positions (in the set), FOR ALL (again) possible moves it leads to a winning state. the closed neighboorhood of each node in the closed neighboorhood of the initial robber position. when cop = robber theres nothing to check (cop wins)
same winning condition as the orignal algo (when there is a full group of marked nodes for some cop config)

for writing:
- always define labels before using them (eg. "let k be the number of cops")
- as precise as possible with the wording

next meeting on friday (no set time yet, will email during the week to decide)