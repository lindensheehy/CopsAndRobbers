# 1/p Visibility Debugging

## 1. Current Problem
- Implementing **1/3 visibility** results in a **7-nested-loop problem** — the direct generalization is not working.

---

## 2. Proposed Approach
- Instead of generalizing directly, **increment step-by-step**: get 1/3 working from the 1/2 implementation, then generalize after that is confirmed correct.

---

## 3. Debugging Note
- Full visibility is working correctly.
- There is likely a **flaw in the 1/2 visibility code** that only becomes apparent when extending to 1/3.
- Investigate 1/2 before assuming the 1/3 logic itself is wrong.

---

## 4. Priority
- Resolve the visibility issue **before** returning to token work.
